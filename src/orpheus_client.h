#pragma once

// Client for orpheus-native-host.exe.  Spawns the 32-bit host process, talks
// the framed TCP protocol and streams rendered audio back to a caller-supplied
// sink.  Works identically from 32-bit and 64-bit processes because the
// transport is a loopback socket.
//
// Latency design: the engine's mute command can take seconds to settle while
// a render is in flight, but a fresh host costs only ~50 ms to spawn and
// initialize.  So an interrupted render is never drained - the connection is
// discarded immediately and a pre-warmed standby host takes over, keeping the
// time from "cancel" to "next utterance speaking" in the tens of
// milliseconds.
//
// One instance per process; all public methods are thread-safe.

// winsock2.h must precede windows.h so the legacy winsock.h is blocked.
#include <winsock2.h>
#include <windows.h>
#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "orpheus_protocol.h"

namespace Orpheus {

struct LangInfo {
    int country;
    std::string name;
};

struct ParamInfo {
    int min_value;
    int max_value;
    int current;
    std::string name;
};

// Receives mono 16-bit 22050 Hz PCM.  Return false to abort the utterance.
typedef bool (*AudioSinkFn)(const void* pcm, uint32_t bytes, void* user);

// Polled between audio chunks (every ~15 ms); return true to abort.
typedef bool (*AbortCheckFn)(void* user);

// One live host process with its socket, reader thread and per-utterance
// audio queue.
class HostConnection {
public:
    HostConnection();
    ~HostConnection();

    HostConnection(const HostConnection&) = delete;
    HostConnection& operator=(const HostConnection&) = delete;

    // Spawn the host, accept its connection and initialize the engine.
    bool start(const std::wstring& host_exe, const std::wstring& data_dir);

    bool alive() const;

    bool send_command(uint16_t command, const void* data, uint32_t size,
                      uint32_t timeout_ms, std::vector<char>* payload_out);

    // Reset the audio queue and start collecting for a new utterance.
    void begin_collecting();
    void stop_collecting();

    // Wait up to wait_ms for an audio chunk; returns false if none arrived.
    bool pop_chunk(std::vector<char>& pcm, uint32_t wait_ms);
    bool done_seen() const;

    // True once the engine has finished the last utterance (its final marker
    // arrived), even if the caller stopped collecting audio.  A host that is
    // idle again can be reused as a standby.
    bool idle() const;

    // Close the socket and terminate the process immediately (fast path used
    // to discard a connection whose render was cancelled).  Safe to call from
    // any thread; idempotent.
    void kill();

private:
    struct PendingResponse {
        HANDLE event = nullptr;
        uint32_t status = 0;
        std::vector<char> payload;
        bool completed = false;
    };

    static DWORD WINAPI reader_thread_proc(LPVOID param);
    void reader_loop();
    bool recv_exact(char* buffer, int size);
    void handle_audio_event(const char* data, uint32_t size);
    bool send_frame(const void* data, uint32_t size);
    void fail_all_pending();

    mutable CRITICAL_SECTION pending_cs_;
    mutable CRITICAL_SECTION audio_cs_;
    CRITICAL_SECTION send_cs_;
    CONDITION_VARIABLE audio_cv_;

    SOCKET socket_ = INVALID_SOCKET;
    HANDLE process_ = nullptr;
    HANDLE reader_thread_ = nullptr;
    volatile bool stop_reader_ = false;
    volatile bool killed_ = false;

    uint32_t next_msg_id_ = 1;
    std::map<uint32_t, PendingResponse*> pending_;

    std::deque<std::vector<char>> audio_queue_;
    bool done_seen_ = false;
    bool collecting_audio_ = false;
    bool speaking_ = false; // engine busy; cleared by the final marker
};

class OrpheusClient {
public:
    static OrpheusClient& instance();

    // Ensure an active host is running; also kicks off the standby spawn.
    // Cheap when everything is already warm - suitable for pre-warming.
    bool ensure_ready();

    bool get_langs(std::vector<LangInfo>& out);
    bool get_params(std::vector<ParamInfo>& out);
    bool get_voices(int country, std::vector<std::string>& out);

    // PARAM_LANGUAGE selects a language by its position in the engine's list,
    // and that list only contains the languages actually installed - so when
    // Setup omits one, every position after it shifts.  Never assume the
    // catalog order: ask the engine once and cache the mapping.
    // Returns false if this country is not installed.
    bool language_index_for(int country, int& index_out);

    // Renders one utterance, streaming audio into sink as it is rendered.
    // `params` carries the engine parameter block; offsets in it are character
    // positions within `text`.  The caller does not add the final index
    // marker - speak_segment appends it.
    //
    // abort_check is polled between chunks.  If the utterance is cancelled
    // while the engine is still rendering, the standby host is promoted
    // immediately - the call never waits for the engine.  The interrupted host
    // is either parked to recover (short utterances finish rendering within
    // tens of milliseconds and then serve as the next standby - pass
    // quick_recover=true) or killed (long renders).  Returns false on
    // transport failure.
    bool speak_segment(const std::wstring& text,
                       const std::vector<protocol::EngineParam>& params,
                       AudioSinkFn sink, AbortCheckFn abort_check, void* user,
                       bool* aborted_out, bool quick_recover = false);

    // Discard every host so the next render picks up a changed voice table.
    // The engine reads intonation, head size and voicing only at host start.
    void recycle_all();

    // Kill every host process (process shutdown).
    void shutdown();

    OrpheusClient(const OrpheusClient&) = delete;
    OrpheusClient& operator=(const OrpheusClient&) = delete;

private:
    OrpheusClient();
    ~OrpheusClient();

    // All _locked methods require state_cs_.
    bool ensure_active_locked();
    void start_standby_spawn_locked();
    void reap_recovering_locked();
    static DWORD WINAPI standby_thread_proc(LPVOID param);
    static void dispose_connection(std::unique_ptr<HostConnection> connection);
    static DWORD WINAPI dispose_thread_proc(LPVOID param);
    bool find_host_paths(std::wstring& host_exe, std::wstring& data_dir) const;

    CRITICAL_SECTION state_cs_;   // guards active_/standby_/spawning flag
    CRITICAL_SECTION render_cs_;  // serializes speak_segment calls

    std::unique_ptr<HostConnection> active_;
    // Pre-warmed hosts ready to take over instantly when a render is
    // cancelled; kept at two so bursts of cancellations (key-repeat
    // character navigation) never pay a synchronous spawn.
    std::vector<std::unique_ptr<HostConnection>> standbys_;
    // An interrupted host finishing a short render; rejoins the standbys once
    // its final marker arrives, or is disposed at the deadline.
    std::unique_ptr<HostConnection> recovering_;
    ULONGLONG recover_deadline_ = 0;
    bool standby_spawning_ = false;
    bool wsa_started_ = false;

    // country -> position in the engine's language list, filled on first use.
    CRITICAL_SECTION lang_cs_;
    std::map<int, int> lang_index_;
    bool lang_index_ready_ = false;
};

}
