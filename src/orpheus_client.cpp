#include "orpheus_client.h"

#include <ws2tcpip.h>
#include <shlwapi.h>
#include <cstring>

#include "orpheus_protocol.h"
#include "debug_log.h"

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "shlwapi.lib")

namespace Orpheus {

namespace {

constexpr uint32_t DEFAULT_COMMAND_TIMEOUT_MS = 5000;
constexpr uint32_t INITIALIZE_TIMEOUT_MS = 30000;
constexpr uint32_t ACCEPT_TIMEOUT_MS = 10000;
constexpr uint32_t CHUNK_POLL_MS = 15;                // abort poll cadence
constexpr ULONGLONG RENDER_TOTAL_TIMEOUT_MS = 120000; // hard cap per segment
constexpr ULONGLONG RENDER_START_TIMEOUT_MS = 15000;  // no audio at all
constexpr ULONGLONG RENDER_IDLE_TIMEOUT_MS = 4000;    // audio stopped, no final marker
constexpr ULONGLONG RECOVER_DEADLINE_MS = 700;        // parked host must go idle by then
constexpr size_t STANDBY_TARGET = 2;                  // pre-warmed hosts kept ready

class ScopedLock {
public:
    explicit ScopedLock(CRITICAL_SECTION* cs) : cs_(cs) { EnterCriticalSection(cs_); }
    ~ScopedLock() { LeaveCriticalSection(cs_); }
    ScopedLock(const ScopedLock&) = delete;
    ScopedLock& operator=(const ScopedLock&) = delete;
private:
    CRITICAL_SECTION* cs_;
};

HMODULE current_module()
{
    HMODULE module = nullptr;
    GetModuleHandleExW(
        GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(&current_module), &module);
    return module;
}

}

// ---------------------------------------------------------------------------
// HostConnection

HostConnection::HostConnection()
{
    InitializeCriticalSection(&pending_cs_);
    InitializeCriticalSection(&audio_cs_);
    InitializeCriticalSection(&send_cs_);
    InitializeConditionVariable(&audio_cv_);
}

HostConnection::~HostConnection()
{
    kill();
    if (reader_thread_) {
        // The reader exits promptly once the socket is shut down; join it
        // BEFORE closing the socket handle so the handle value cannot be
        // recycled into a new connection while recv() still references it.
        WaitForSingleObject(reader_thread_, 5000);
        CloseHandle(reader_thread_);
        reader_thread_ = nullptr;
    }
    if (socket_ != INVALID_SOCKET) {
        closesocket(socket_);
        socket_ = INVALID_SOCKET;
    }
    if (process_) {
        CloseHandle(process_);
        process_ = nullptr;
    }
    DeleteCriticalSection(&pending_cs_);
    DeleteCriticalSection(&audio_cs_);
    DeleteCriticalSection(&send_cs_);
}

bool HostConnection::start(const std::wstring& host_exe, const std::wstring& data_dir)
{
    SOCKET listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listener == INVALID_SOCKET) {
        return false;
    }

    sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;
    int addr_len = sizeof(addr);
    if (bind(listener, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0 ||
        listen(listener, 1) != 0 ||
        getsockname(listener, reinterpret_cast<sockaddr*>(&addr), &addr_len) != 0) {
        closesocket(listener);
        return false;
    }
    const int port = ntohs(addr.sin_port);

    wchar_t command_line[MAX_PATH * 2];
    swprintf_s(command_line, L"\"%s\" --address 127.0.0.1:%d", host_exe.c_str(), port);

    std::wstring host_dir = host_exe;
    PathRemoveFileSpecW(host_dir.data());
    host_dir.resize(wcslen(host_dir.c_str()));

    STARTUPINFOW si = { sizeof(si) };
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi = {};
    if (!CreateProcessW(host_exe.c_str(), command_line, nullptr, nullptr, FALSE,
                        CREATE_NO_WINDOW, nullptr, host_dir.c_str(), &si, &pi)) {
        ORPHEUS_LOG("Host: CreateProcess failed, error %lu", GetLastError());
        closesocket(listener);
        return false;
    }
    CloseHandle(pi.hThread);
    process_ = pi.hProcess;

    fd_set read_set;
    FD_ZERO(&read_set);
    FD_SET(listener, &read_set);
    timeval timeout = { static_cast<long>(ACCEPT_TIMEOUT_MS / 1000),
                        static_cast<long>((ACCEPT_TIMEOUT_MS % 1000) * 1000) };
    if (select(0, &read_set, nullptr, nullptr, &timeout) != 1) {
        ORPHEUS_LOG("Host: no connect-back within %u ms", ACCEPT_TIMEOUT_MS);
        closesocket(listener);
        kill();
        return false;
    }
    socket_ = accept(listener, nullptr, nullptr);
    closesocket(listener);
    if (socket_ == INVALID_SOCKET) {
        kill();
        return false;
    }
    BOOL no_delay = TRUE;
    setsockopt(socket_, IPPROTO_TCP, TCP_NODELAY,
               reinterpret_cast<const char*>(&no_delay), sizeof(no_delay));

    reader_thread_ = CreateThread(nullptr, 0, reader_thread_proc, this, 0, nullptr);
    if (!reader_thread_) {
        kill();
        return false;
    }

    // utf8_len includes the NUL terminator; allocate room for it, convert,
    // then drop it - the wire format is a length-prefixed string without NUL.
    const int utf8_len = WideCharToMultiByte(CP_UTF8, 0, data_dir.c_str(), -1,
                                             nullptr, 0, nullptr, nullptr);
    if (utf8_len <= 0) {
        kill();
        return false;
    }
    std::vector<char> payload(4 + static_cast<size_t>(utf8_len));
    const uint32_t path_len = static_cast<uint32_t>(utf8_len - 1);
    memcpy(payload.data(), &path_len, 4);
    WideCharToMultiByte(CP_UTF8, 0, data_dir.c_str(), -1,
                        payload.data() + 4, utf8_len, nullptr, nullptr);
    payload.resize(4 + path_len);
    if (!send_command(protocol::CMD_INITIALIZE, payload.data(),
                      static_cast<uint32_t>(payload.size()), INITIALIZE_TIMEOUT_MS, nullptr)) {
        ORPHEUS_LOG("Host: engine initialize failed");
        kill();
        return false;
    }

    ORPHEUS_LOG("Host: started pid %lu on port %d", pi.dwProcessId, port);
    return true;
}

bool HostConnection::alive() const
{
    return !killed_ && process_ &&
           WaitForSingleObject(process_, 0) == WAIT_TIMEOUT &&
           socket_ != INVALID_SOCKET;
}

void HostConnection::kill()
{
    killed_ = true;
    stop_reader_ = true;
    if (socket_ != INVALID_SOCKET) {
        // shutdown() only: it unblocks the reader's recv() but keeps the
        // handle allocated, so the value cannot be reused by another
        // connection while the reader is still winding down.  The destructor
        // closes the handle after joining the reader.
        ::shutdown(socket_, SD_BOTH);
    }
    if (process_) {
        TerminateProcess(process_, 0);
    }
    fail_all_pending();
    {
        ScopedLock lock(&audio_cs_);
        done_seen_ = true;
        WakeAllConditionVariable(&audio_cv_);
    }
}

void HostConnection::fail_all_pending()
{
    ScopedLock lock(&pending_cs_);
    for (auto& entry : pending_) {
        entry.second->status = 0xFFFFFFFF;
        entry.second->completed = true;
        SetEvent(entry.second->event);
    }
    pending_.clear();
}

bool HostConnection::send_frame(const void* data, uint32_t size)
{
    ScopedLock lock(&send_cs_);
    if (socket_ == INVALID_SOCKET) {
        return false;
    }
    std::vector<char> frame(4 + size);
    memcpy(frame.data(), &size, 4);
    memcpy(frame.data() + 4, data, size);
    const char* ptr = frame.data();
    int remaining = static_cast<int>(frame.size());
    while (remaining > 0) {
        const int sent = send(socket_, ptr, remaining, 0);
        if (sent <= 0) {
            return false;
        }
        ptr += sent;
        remaining -= sent;
    }
    return true;
}

bool HostConnection::send_command(uint16_t command, const void* data, uint32_t size,
                                  uint32_t timeout_ms, std::vector<char>* payload_out)
{
    PendingResponse response;
    response.event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!response.event) {
        return false;
    }

    uint32_t msg_id;
    {
        ScopedLock lock(&pending_cs_);
        msg_id = next_msg_id_++;
        pending_[msg_id] = &response;
    }

    std::vector<char> frame(7 + size);
    frame[0] = static_cast<char>(protocol::FRAME_COMMAND);
    memcpy(frame.data() + 1, &msg_id, 4);
    memcpy(frame.data() + 5, &command, 2);
    if (size > 0) {
        memcpy(frame.data() + 7, data, size);
    }

    bool ok = send_frame(frame.data(), static_cast<uint32_t>(frame.size()));
    if (ok) {
        ok = WaitForSingleObject(response.event, timeout_ms) == WAIT_OBJECT_0;
        if (!ok) {
            ORPHEUS_LOG("Host: command %u timed out after %u ms", command, timeout_ms);
        }
    }

    {
        ScopedLock lock(&pending_cs_);
        pending_.erase(msg_id);
    }
    CloseHandle(response.event);

    if (!ok || !response.completed || response.status != protocol::STATUS_OK) {
        if (ok && response.completed && response.status != protocol::STATUS_OK) {
            ORPHEUS_LOG("Host: command %u failed with status %u: %.*s", command,
                        response.status, static_cast<int>(response.payload.size()),
                        response.payload.data());
        }
        return false;
    }
    if (payload_out) {
        *payload_out = std::move(response.payload);
    }
    return true;
}

DWORD WINAPI HostConnection::reader_thread_proc(LPVOID param)
{
    static_cast<HostConnection*>(param)->reader_loop();
    return 0;
}

bool HostConnection::recv_exact(char* buffer, int size)
{
    while (size > 0) {
        const int received = recv(socket_, buffer, size, 0);
        if (received <= 0) {
            return false;
        }
        buffer += received;
        size -= received;
    }
    return true;
}

void HostConnection::reader_loop()
{
    std::vector<char> frame;
    while (!stop_reader_) {
        uint32_t length = 0;
        if (!recv_exact(reinterpret_cast<char*>(&length), 4)) {
            break;
        }
        if (length == 0 || length > 64u * 1024 * 1024) {
            break;
        }
        frame.resize(length);
        if (!recv_exact(frame.data(), static_cast<int>(length))) {
            break;
        }

        const uint8_t frame_type = static_cast<uint8_t>(frame[0]);
        if (frame_type == protocol::FRAME_RESPONSE && length >= 9) {
            uint32_t msg_id, status;
            memcpy(&msg_id, frame.data() + 1, 4);
            memcpy(&status, frame.data() + 5, 4);
            ScopedLock lock(&pending_cs_);
            auto it = pending_.find(msg_id);
            if (it != pending_.end()) {
                it->second->status = status;
                it->second->payload.assign(frame.begin() + 9, frame.end());
                it->second->completed = true;
                SetEvent(it->second->event);
            }
        } else if (frame_type == protocol::FRAME_EVENT && length >= 3) {
            uint16_t event_id;
            memcpy(&event_id, frame.data() + 1, 2);
            if (event_id == protocol::EV_AUDIO) {
                handle_audio_event(frame.data() + 3, length - 3);
            }
        }
    }
    fail_all_pending();
    {
        ScopedLock lock(&audio_cs_);
        done_seen_ = true;
        WakeAllConditionVariable(&audio_cv_);
    }
}

void HostConnection::handle_audio_event(const char* data, uint32_t size)
{
    if (size < 4) {
        return;
    }
    uint32_t audio_len = 0;
    memcpy(&audio_len, data, 4);
    if (static_cast<uint64_t>(audio_len) + 8 > size) {
        return;
    }
    const char* audio = data + 4;
    uint32_t controls_len = 0;
    memcpy(&controls_len, data + 4 + audio_len, 4);
    const char* controls = data + 4 + audio_len + 4;
    if (static_cast<uint64_t>(audio_len) + 8 + controls_len > size) {
        controls_len = 0;
    }

    // This engine already renders one channel - unlike Orpheus Classic, which
    // emitted duplicated stereo.  Pass the samples straight through; halving
    // them here would double the pitch and the speed.
    std::vector<char> mono(audio, audio + (audio_len & ~1u));

    bool final_marker = false;
    for (uint32_t offset = 0; offset + 12 <= controls_len; offset += 12) {
        uint32_t value = 0;
        memcpy(&value, controls + offset + 8, 4);
        if (value & protocol::CONTROL_FINAL_FLAG) {
            final_marker = true;
        }
    }

    ScopedLock lock(&audio_cs_);
    if (final_marker) {
        speaking_ = false; // tracked even when the caller stopped collecting
    }
    if (collecting_audio_) {
        if (!mono.empty()) {
            audio_queue_.push_back(std::move(mono));
        }
        if (final_marker) {
            done_seen_ = true;
        }
        WakeAllConditionVariable(&audio_cv_);
    }
}

void HostConnection::begin_collecting()
{
    ScopedLock lock(&audio_cs_);
    audio_queue_.clear();
    done_seen_ = false;
    collecting_audio_ = true;
    speaking_ = true;
}

void HostConnection::stop_collecting()
{
    ScopedLock lock(&audio_cs_);
    collecting_audio_ = false;
    audio_queue_.clear();
}

bool HostConnection::pop_chunk(std::vector<char>& mono_pcm, uint32_t wait_ms)
{
    ScopedLock lock(&audio_cs_);
    if (audio_queue_.empty() && !done_seen_) {
        SleepConditionVariableCS(&audio_cv_, &audio_cs_, wait_ms);
    }
    if (!audio_queue_.empty()) {
        mono_pcm = std::move(audio_queue_.front());
        audio_queue_.pop_front();
        return true;
    }
    return false;
}

bool HostConnection::done_seen() const
{
    ScopedLock lock(&audio_cs_);
    return done_seen_ && audio_queue_.empty();
}

bool HostConnection::idle() const
{
    ScopedLock lock(&audio_cs_);
    return !speaking_;
}

// ---------------------------------------------------------------------------
// OrpheusClient

OrpheusClient& OrpheusClient::instance()
{
    static OrpheusClient client;
    return client;
}

OrpheusClient::OrpheusClient()
{
    InitializeCriticalSection(&state_cs_);
    InitializeCriticalSection(&render_cs_);
    InitializeCriticalSection(&lang_cs_);
}

bool OrpheusClient::language_index_for(int country, int& index_out)
{
    {
        ScopedLock lock(&lang_cs_);
        if (lang_index_ready_) {
            auto it = lang_index_.find(country);
            if (it == lang_index_.end()) {
                return false;
            }
            index_out = it->second;
            return true;
        }
    }

    std::vector<LangInfo> langs;
    if (!get_langs(langs) || langs.empty()) {
        return false;
    }

    ScopedLock lock(&lang_cs_);
    lang_index_.clear();
    for (size_t i = 0; i < langs.size(); ++i) {
        lang_index_[langs[i].country] = static_cast<int>(i);
    }
    lang_index_ready_ = true;
    ORPHEUS_LOG("Client: engine offers %u languages", static_cast<unsigned>(langs.size()));
    auto it = lang_index_.find(country);
    if (it == lang_index_.end()) {
        ORPHEUS_LOG("Client: country %d is not installed", country);
        return false;
    }
    index_out = it->second;
    return true;
}

OrpheusClient::~OrpheusClient()
{
    // Process-lifetime singleton: leave teardown to the OS.  Host processes
    // exit on their own when the sockets close.
}

bool OrpheusClient::find_host_paths(std::wstring& host_exe, std::wstring& data_dir) const
{
    std::vector<std::wstring> bases;

    wchar_t env_home[MAX_PATH] = {};
    if (GetEnvironmentVariableW(L"ORPHEUS_NATIVE_SAPI_HOME", env_home, MAX_PATH) > 0) {
        bases.emplace_back(env_home);
    }

    wchar_t module_path[MAX_PATH] = {};
    if (HMODULE module = current_module()) {
        if (GetModuleFileNameW(module, module_path, MAX_PATH) > 0) {
            PathRemoveFileSpecW(module_path);
            bases.emplace_back(module_path);       // alongside this module
            PathRemoveFileSpecW(module_path);
            bases.emplace_back(module_path);       // parent (x64 DLL lives in {app}\x64)
        }
    }

    for (const auto& base : bases) {
        std::wstring exe = base + L"\\orpheus-native-host.exe";
        std::wstring data = base + L"\\orpheus";
        const DWORD data_attributes = GetFileAttributesW(data.c_str());
        if (GetFileAttributesW(exe.c_str()) != INVALID_FILE_ATTRIBUTES &&
            data_attributes != INVALID_FILE_ATTRIBUTES &&
            (data_attributes & FILE_ATTRIBUTE_DIRECTORY)) {
            host_exe = exe;
            data_dir = data;
            return true;
        }
    }
    ORPHEUS_LOG("Client: host exe / orpheus data dir not found (%u candidate dirs)",
                static_cast<unsigned>(bases.size()));
    return false;
}

bool OrpheusClient::ensure_ready()
{
    ScopedLock lock(&state_cs_);
    if (!ensure_active_locked()) {
        return false;
    }
    start_standby_spawn_locked();
    return true;
}

// Fold a parked (interrupted) host back into the pool: once its final marker
// has arrived it is as good as a fresh standby; past the deadline or dead it
// is disposed.
void OrpheusClient::reap_recovering_locked()
{
    if (!recovering_) {
        return;
    }
    if (!recovering_->alive()) {
        dispose_connection(std::move(recovering_));
        return;
    }
    if (recovering_->idle()) {
        if (standbys_.size() < STANDBY_TARGET) {
            ORPHEUS_LOG("Client: recovered host becomes standby");
            standbys_.push_back(std::move(recovering_));
        } else {
            dispose_connection(std::move(recovering_));
        }
        return;
    }
    if (GetTickCount64() > recover_deadline_) {
        ORPHEUS_LOG("Client: recovering host missed its deadline; disposing");
        dispose_connection(std::move(recovering_));
    }
}

bool OrpheusClient::ensure_active_locked()
{
    if (!wsa_started_) {
        WSADATA wsa_data;
        if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0) {
            ORPHEUS_LOG("Client: WSAStartup failed");
            return false;
        }
        wsa_started_ = true;
    }

    reap_recovering_locked();

    if (active_ && active_->alive()) {
        return true;
    }
    if (active_) {
        dispose_connection(std::move(active_));
    }

    // Promote a standby if one is ready.
    while (!standbys_.empty()) {
        std::unique_ptr<HostConnection> candidate = std::move(standbys_.back());
        standbys_.pop_back();
        if (candidate->alive()) {
            active_ = std::move(candidate);
            ORPHEUS_LOG("Client: standby promoted to active");
            return true;
        }
        dispose_connection(std::move(candidate));
    }

    // Cold path: build one synchronously (~50 ms).
    std::wstring host_exe, data_dir;
    if (!find_host_paths(host_exe, data_dir)) {
        return false;
    }
    auto connection = std::make_unique<HostConnection>();
    if (!connection->start(host_exe, data_dir)) {
        return false;
    }
    active_ = std::move(connection);
    return true;
}

namespace {

struct StandbyThreadContext {
    OrpheusClient* client;
    std::wstring host_exe;
    std::wstring data_dir;
    // Written back under state_cs_ by the thread.
};

struct DisposeThreadContext {
    HostConnection* connection;
};

}

DWORD WINAPI OrpheusClient::standby_thread_proc(LPVOID param)
{
    auto* context = static_cast<StandbyThreadContext*>(param);
    OrpheusClient* client = context->client;

    // Keep spawning until the standby pool is full (or a spawn fails).
    for (;;) {
        auto connection = std::make_unique<HostConnection>();
        const bool ok = connection->start(context->host_exe, context->data_dir);

        bool keep_going = false;
        EnterCriticalSection(&client->state_cs_);
        if (ok) {
            if (client->standbys_.size() < STANDBY_TARGET) {
                client->standbys_.push_back(std::move(connection));
            }
            keep_going = client->standbys_.size() < STANDBY_TARGET;
        }
        if (!keep_going) {
            client->standby_spawning_ = false;
        }
        LeaveCriticalSection(&client->state_cs_);

        if (connection) {
            // Spawn failed or the pool was already full; discard.
            connection->kill();
        }
        if (!keep_going) {
            break;
        }
    }
    delete context;
    return 0;
}

void OrpheusClient::start_standby_spawn_locked()
{
    reap_recovering_locked();
    for (size_t i = standbys_.size(); i-- > 0;) {
        if (!standbys_[i]->alive()) {
            dispose_connection(std::move(standbys_[i]));
            standbys_.erase(standbys_.begin() + i);
        }
    }
    if (standbys_.size() >= STANDBY_TARGET || standby_spawning_) {
        return;
    }
    std::wstring host_exe, data_dir;
    if (!find_host_paths(host_exe, data_dir)) {
        return;
    }
    auto* context = new StandbyThreadContext{ this, std::move(host_exe), std::move(data_dir) };
    HANDLE thread = CreateThread(nullptr, 0, standby_thread_proc, context, 0, nullptr);
    if (thread) {
        standby_spawning_ = true;
        CloseHandle(thread);
    } else {
        delete context;
    }
}

DWORD WINAPI OrpheusClient::dispose_thread_proc(LPVOID param)
{
    auto* context = static_cast<DisposeThreadContext*>(param);
    delete context->connection; // destructor kills and joins the reader
    delete context;
    return 0;
}

void OrpheusClient::dispose_connection(std::unique_ptr<HostConnection> connection)
{
    if (!connection) {
        return;
    }
    connection->kill(); // instant: shut down socket + TerminateProcess
    auto* context = new DisposeThreadContext{ connection.release() };
    HANDLE thread = CreateThread(nullptr, 0, dispose_thread_proc, context, 0, nullptr);
    if (thread) {
        CloseHandle(thread);
    } else {
        delete context->connection;
        delete context;
    }
}

bool OrpheusClient::get_langs(std::vector<LangInfo>& out)
{
    out.clear();
    if (!ensure_ready()) {
        return false;
    }
    std::vector<char> payload;
    {
        ScopedLock lock(&state_cs_);
        if (!active_ ||
            !active_->send_command(protocol::CMD_GET_LANGS, nullptr, 0,
                                   DEFAULT_COMMAND_TIMEOUT_MS, &payload)) {
            return false;
        }
    }
    size_t offset = 0;
    auto read_u32 = [&](uint32_t& value) {
        if (offset + 4 > payload.size()) return false;
        memcpy(&value, payload.data() + offset, 4);
        offset += 4;
        return true;
    };
    auto read_string = [&](std::string& value) {
        uint32_t length = 0;
        if (!read_u32(length) || offset + length > payload.size()) return false;
        value.assign(payload.data() + offset, length);
        offset += length;
        return true;
    };
    uint32_t count = 0;
    if (!read_u32(count)) {
        return false;
    }
    // Each entry is <u32 country><u32 len><utf-8 name>.  (Orpheus Classic sent
    // a language code string as well; this engine does not.)
    for (uint32_t i = 0; i < count; ++i) {
        LangInfo info;
        uint32_t country = 0;
        if (!read_u32(country) || !read_string(info.name)) {
            return false;
        }
        info.country = static_cast<int>(country);
        out.push_back(std::move(info));
    }
    return true;
}

bool OrpheusClient::get_voices(int country, std::vector<std::string>& out)
{
    out.clear();
    if (!ensure_ready()) {
        return false;
    }
    const uint32_t country_value = static_cast<uint32_t>(country);
    std::vector<char> payload;
    {
        ScopedLock lock(&state_cs_);
        if (!active_ ||
            !active_->send_command(protocol::CMD_GET_VOICES, &country_value,
                                   sizeof(country_value), DEFAULT_COMMAND_TIMEOUT_MS,
                                   &payload)) {
            return false;
        }
    }
    if (payload.size() < 4) {
        return false;
    }
    uint32_t count = 0;
    memcpy(&count, payload.data(), 4);
    size_t offset = 4;
    for (uint32_t i = 0; i < count; ++i) {
        if (offset + 4 > payload.size()) {
            return false;
        }
        uint32_t length = 0;
        memcpy(&length, payload.data() + offset, 4);
        offset += 4;
        if (offset + length > payload.size()) {
            return false;
        }
        out.emplace_back(payload.data() + offset, length);
        offset += length;
    }
    return true;
}

bool OrpheusClient::get_params(std::vector<ParamInfo>& out)
{
    out.clear();
    if (!ensure_ready()) {
        return false;
    }
    std::vector<char> payload;
    {
        ScopedLock lock(&state_cs_);
        if (!active_ ||
            !active_->send_command(protocol::CMD_GET_PARAMS, nullptr, 0,
                                   DEFAULT_COMMAND_TIMEOUT_MS, &payload)) {
            return false;
        }
    }
    if (payload.size() < 4) {
        return false;
    }
    uint32_t count = 0;
    memcpy(&count, payload.data(), 4);
    size_t offset = 4;
    for (uint32_t i = 0; i < count; ++i) {
        if (offset + 16 > payload.size()) {
            return false;
        }
        ParamInfo info;
        memcpy(&info.min_value, payload.data() + offset, 4);
        memcpy(&info.max_value, payload.data() + offset + 4, 4);
        memcpy(&info.current, payload.data() + offset + 8, 4);
        uint32_t name_len = 0;
        memcpy(&name_len, payload.data() + offset + 12, 4);
        offset += 16;
        if (offset + name_len > payload.size()) {
            return false;
        }
        info.name.assign(payload.data() + offset, name_len);
        offset += name_len;
        out.push_back(std::move(info));
    }
    return true;
}

bool OrpheusClient::speak_segment(const std::wstring& text,
                                  const std::vector<protocol::EngineParam>& params,
                                  AudioSinkFn sink, AbortCheckFn abort_check, void* user,
                                  bool* aborted_out, bool quick_recover)
{
    if (aborted_out) {
        *aborted_out = false;
    }

    ScopedLock render_lock(&render_cs_);

    HostConnection* host = nullptr;
    {
        ScopedLock lock(&state_cs_);
        if (!ensure_active_locked()) {
            return false;
        }
        start_standby_spawn_locked();
        host = active_.get();
    }

    host->begin_collecting();

    // The parameter block is the caller's list plus the final index marker,
    // which must sit on the last character of the text: put it at offset 0 and
    // the engine reports the utterance finished before it has said anything.
    std::vector<protocol::EngineParam> block = params;
    protocol::EngineParam marker = {};
    marker.offset = text.empty() ? 0 : static_cast<uint32_t>(text.size() - 1);
    marker.type = 0;
    marker.id = protocol::PARAM_INDEX;
    marker.value = static_cast<int32_t>(protocol::CONTROL_FINAL_FLAG);
    block.push_back(marker);

    const uint32_t text_bytes = static_cast<uint32_t>(text.size() * sizeof(wchar_t));
    const uint32_t params_len =
        static_cast<uint32_t>(block.size() * sizeof(protocol::EngineParam));
    std::vector<char> payload(8 + params_len + text_bytes);
    memcpy(payload.data(), &params_len, 4);
    memcpy(payload.data() + 4, &text_bytes, 4);
    memcpy(payload.data() + 8, block.data(), params_len);
    memcpy(payload.data() + 8 + params_len, text.data(), text_bytes);

    if (!host->send_command(protocol::CMD_APPEND, payload.data(),
                            static_cast<uint32_t>(payload.size()),
                            DEFAULT_COMMAND_TIMEOUT_MS, nullptr) ||
        !host->send_command(protocol::CMD_SPEAK_APPEND, nullptr, 0,
                            DEFAULT_COMMAND_TIMEOUT_MS, nullptr)) {
        host->stop_collecting();
        ScopedLock lock(&state_cs_);
        dispose_connection(std::move(active_));
        return false;
    }

    const ULONGLONG start_tick = GetTickCount64();
    ULONGLONG last_audio_tick = 0;
    bool aborted = false;
    bool transport_ok = true;

    std::vector<char> chunk;
    for (;;) {
        if (!aborted && abort_check && abort_check(user)) {
            aborted = true;
        }

        if (!aborted && host->pop_chunk(chunk, CHUNK_POLL_MS)) {
            last_audio_tick = GetTickCount64();
            if (sink && !chunk.empty() &&
                !sink(chunk.data(), static_cast<uint32_t>(chunk.size()), user)) {
                aborted = true;
            }
            if (!aborted) {
                continue;
            }
        }

        if (aborted) {
            break;
        }

        if (host->done_seen()) {
            break;
        }

        const ULONGLONG now = GetTickCount64();
        if (!host->alive()) {
            ORPHEUS_LOG("Client: host died during render");
            transport_ok = false;
            break;
        }
        if (last_audio_tick == 0 && now - start_tick > RENDER_START_TIMEOUT_MS) {
            ORPHEUS_LOG("Client: no audio within %llu ms; giving up", RENDER_START_TIMEOUT_MS);
            transport_ok = false;
            break;
        }
        if (last_audio_tick != 0 && now - last_audio_tick > RENDER_IDLE_TIMEOUT_MS) {
            // Final marker never arrived; assume the utterance finished.
            break;
        }
        if (now - start_tick > RENDER_TOTAL_TIMEOUT_MS) {
            ORPHEUS_LOG("Client: render exceeded %llu ms; cancelling", RENDER_TOTAL_TIMEOUT_MS);
            aborted = true;
            break;
        }
    }

    host->stop_collecting();

    if (!transport_ok || (aborted && !host->done_seen())) {
        // The engine is (or may be) still rendering.  Never wait for it: the
        // mute command can take seconds to settle, while a fresh host costs
        // ~50 ms.  The standby takes over immediately.  A short render (for
        // example a single character during key-repeat navigation) finishes
        // within tens of milliseconds, so that host is parked and comes back
        // as the next standby instead of being killed - sustained character
        // arrowing then ping-pongs between two hosts with no process churn.
        ScopedLock lock(&state_cs_);
        if (active_.get() == host) {
            if (transport_ok && quick_recover && host->alive()) {
                reap_recovering_locked();
                if (recovering_) {
                    // An earlier render is still finishing; it is closer to
                    // idle than the newcomer, so keep it and kill this one.
                    ORPHEUS_LOG("Client: recovery slot busy; discarding newcomer");
                    dispose_connection(std::move(active_));
                } else {
                    ORPHEUS_LOG("Client: parking mid-render host to recover");
                    recovering_ = std::move(active_);
                    recover_deadline_ = GetTickCount64() + RECOVER_DEADLINE_MS;
                }
            } else {
                ORPHEUS_LOG("Client: discarding mid-render host (aborted=%d)",
                            aborted ? 1 : 0);
                dispose_connection(std::move(active_));
            }
            ensure_active_locked();
            start_standby_spawn_locked();
        }
    }

    if (aborted_out) {
        *aborted_out = aborted;
    }
    return transport_ok;
}

void OrpheusClient::recycle_all()
{
    // Intonation, head size and voicing are read from the engine's voice table
    // when a host starts, so a change to them only becomes audible once every
    // host - active, standby and recovering - has been replaced.
    ScopedLock render_lock(&render_cs_);
    {
        ScopedLock lock(&state_cs_);
        if (active_) {
            dispose_connection(std::move(active_));
        }
        for (auto& standby : standbys_) {
            dispose_connection(std::move(standby));
        }
        standbys_.clear();
        if (recovering_) {
            dispose_connection(std::move(recovering_));
        }
        ORPHEUS_LOG("Client: recycling all hosts after a voice-table change");
    }
    ensure_ready();
}

void OrpheusClient::shutdown()
{
    ScopedLock render_lock(&render_cs_);
    ScopedLock lock(&state_cs_);
    if (active_) {
        dispose_connection(std::move(active_));
    }
    for (auto& standby : standbys_) {
        dispose_connection(std::move(standby));
    }
    standbys_.clear();
    if (recovering_) {
        dispose_connection(std::move(recovering_));
    }
}

}
