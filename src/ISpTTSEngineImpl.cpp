#include "orpheus_client.h" // must come first: winsock2.h before windows.h

#include <new>
#include <string>
#include <vector>
#include <cmath>
#include <cwctype>
#include <algorithm>

#include "utils.hpp"
#include "ISpTTSEngineImpl.hpp"
#include "orpheus_protocol.h"
#include "settings.h"
#include "voicedesc.h"
#include "debug_log.h"

namespace Orpheus {
namespace sapi {

namespace {

constexpr WORD AUDIO_CHANNELS = static_cast<WORD>(protocol::HOST_CHANNELS);
constexpr DWORD AUDIO_SAMPLE_RATE = protocol::HOST_SAMPLE_RATE;
constexpr WORD AUDIO_BITS_PER_SAMPLE = static_cast<WORD>(protocol::HOST_BITS);
constexpr DWORD AUDIO_BYTES_PER_SEC =
    AUDIO_SAMPLE_RATE * AUDIO_BITS_PER_SAMPLE / 8 * AUDIO_CHANNELS;

constexpr int SAPI_RATE_MIN = -10;
constexpr int SAPI_RATE_MAX = 10;
constexpr int SAPI_PITCH_ADJ_RANGE = 24; // SAPI pitch units are 1/24 octave

// A segment is one host render; bounded so that a cancel never has to wait
// for a long render to wind down.
constexpr size_t MAX_SEGMENT_CHARS = 600;

// Segments at most this long (single characters and short words - the things
// a screen reader user arrows through rapidly) render within a few hundred
// milliseconds, so an interrupted host can finish quietly and be reused
// instead of being killed.
constexpr size_t QUICK_RECOVER_MAX_CHARS = 48;

[[nodiscard]] int clamp_int(int value, int min_value, int max_value)
{
    return (std::max)(min_value, (std::min)(max_value, value));
}

// SAPI rate -10..10 sweeps the engine range exponentially around the base.
[[nodiscard]] int engine_rate(int base_rate, long sapi_rate)
{
    using namespace protocol;
    const int rate = clamp_int(base_rate, RATE_MIN, RATE_MAX);
    const long adj = clamp_int(static_cast<int>(sapi_rate), SAPI_RATE_MIN, SAPI_RATE_MAX);
    double value = rate;
    if (adj > 0) {
        value = rate * std::pow(static_cast<double>(RATE_MAX) / rate, adj / 10.0);
    } else if (adj < 0) {
        value = rate * std::pow(static_cast<double>(RATE_MIN) / rate, -adj / 10.0);
    }
    return clamp_int(static_cast<int>(std::lround(value)), RATE_MIN, RATE_MAX);
}

// SAPI pitch adjustment is in 1/24 octave steps.
[[nodiscard]] int engine_pitch(int base_pitch, long middle_adj)
{
    using namespace protocol;
    const int pitch = clamp_int(base_pitch, PITCH_MIN, PITCH_MAX);
    const long adj =
        clamp_int(static_cast<int>(middle_adj), -SAPI_PITCH_ADJ_RANGE, SAPI_PITCH_ADJ_RANGE);
    const double value = pitch * std::pow(2.0, adj / static_cast<double>(SAPI_PITCH_ADJ_RANGE));
    return clamp_int(static_cast<int>(std::lround(value)), PITCH_MIN, PITCH_MAX);
}

[[nodiscard]] int engine_volume(int base_volume, unsigned short sapi_volume, ULONG frag_volume)
{
    const double value = clamp_int(base_volume, 0, 100) / 100.0 *
                         clamp_int(sapi_volume, 0, 100) / 100.0 *
                         clamp_int(static_cast<int>(frag_volume), 0, 100) / 100.0 * 100.0;
    return clamp_int(static_cast<int>(std::lround(value)), 0, 100);
}

// Eighteen ASCII characters render as silence - about 20 ms of nothing - in
// both normal and spelling mode.  That is fine inside prose, where a hyphen
// should not be announced, and useless when the character *is* the utterance:
// a screen reader user arrowing across "a + b" would hear nothing at all for
// the plus.  So when a lone character is being read out, one that the engine
// will not voice is replaced by its name.
//
// Screen readers normally substitute their own symbol names before the text
// reaches a synthesiser, and those win because they arrive as ordinary words.
// This is the fallback for clients that do not.
[[nodiscard]] const wchar_t* silent_char_name(wchar_t ch)
{
    switch (ch) {
    case L'@':  return L"at";
    case L'#':  return L"number";
    case L'%':  return L"percent";
    case L'&':  return L"and";
    case L'*':  return L"star";
    case L'-':  return L"dash";
    case L'+':  return L"plus";
    case L'=':  return L"equals";
    case L'/':  return L"slash";
    case L'_':  return L"underline";
    case L'<':  return L"less than";
    case L'>':  return L"greater than";
    case L'\\': return L"backslash";
    case L'|':  return L"bar";
    case L'~':  return L"tilde";
    case L'^':  return L"caret";
    case L'"':  return L"quote";
    case L'`':  return L"back tick";
    default:    return nullptr;
    }
}

// Normalise characters the engine mispronounces.  Unlike Orpheus Classic
// there is no inline "@<...>" command syntax to defuse here: every parameter
// travels in the binary parameter block, so user text is never parsed for
// commands and needs no escaping.
void append_sanitized(std::wstring& out, const wchar_t* text, size_t length)
{
    for (size_t i = 0; i < length; ++i) {
        wchar_t ch = text[i];
        switch (ch) {
        case 0x2019: ch = L'\''; break; // right single quotation mark
        case 0x201C:                    // left double quotation mark
        case 0x201D: ch = L'"'; break;  // right double quotation mark
        case L'\r':
        case L'\n':
        case L'\t': ch = L' '; break;
        default:
            if (ch != 0 && ch < 0x20) {
                ch = L' ';
            }
            break;
        }
        out += ch;
    }
}

struct UpfrontEvent {
    SPEVENTENUM event_id;
    ULONG text_offset;
    ULONG text_length;
};

// A parameter change that takes effect at a character offset in the body.
struct ParamChange {
    size_t offset;
    int id;
    int value;
};

struct WorkItem {
    enum class Type { Text, Bookmark, Silence };
    Type type = Type::Text;
    std::wstring text;
    std::vector<UpfrontEvent> events;
    std::vector<ParamChange> changes; // offsets relative to `text`
    ULONG silence_ms = 0;
};

struct SpeakContext {
    ISpTTSEngineSite* site = nullptr;
    ULONGLONG bytes_written = 0;
    bool aborted = false;
    bool skip_requested = false;
};

// Polled between audio chunks so cancellation is noticed within ~15 ms even
// while the engine is still rendering.
bool abort_check(void* user)
{
    auto* ctx = static_cast<SpeakContext*>(user);
    const DWORD actions = ctx->site->GetActions();
    if (actions & SPVES_ABORT) {
        ctx->aborted = true;
        return true;
    }
    if (actions & SPVES_SKIP) {
        ctx->site->CompleteSkip(0);
        ctx->skip_requested = true;
        ctx->aborted = true;
        return true;
    }
    return false;
}

bool write_audio(SpeakContext& ctx, const void* data, uint32_t size)
{
    if (ctx.bytes_written == 0) {
        ORPHEUS_LOG("Speak: first audio write (%u bytes)", size);
    }
    const BYTE* ptr = static_cast<const BYTE*>(data);
    ULONG remaining = size;
    while (remaining > 0) {
        const DWORD actions = ctx.site->GetActions();
        if (actions & SPVES_ABORT) {
            ctx.aborted = true;
            return false;
        }
        if (actions & SPVES_SKIP) {
            ctx.site->CompleteSkip(0);
            ctx.skip_requested = true;
            ctx.aborted = true;
            return false;
        }
        ULONG written = 0;
        const HRESULT hr = ctx.site->Write(ptr, remaining, &written);
        if (FAILED(hr)) {
            ORPHEUS_LOG("Speak: site Write failed, hr=0x%08lX", static_cast<unsigned long>(hr));
            ctx.aborted = true;
            return false;
        }
        // Some SAPI sites do not fill pcbWritten reliably; on success treat
        // the whole buffer as consumed unless a smaller value was returned.
        if (written == 0 || written > remaining) {
            written = remaining;
        }
        ctx.bytes_written += written;
        remaining -= written;
        ptr += written;
    }
    return true;
}

bool audio_sink(const void* pcm, uint32_t bytes, void* user)
{
    return write_audio(*static_cast<SpeakContext*>(user), pcm, bytes);
}

void collect_boundary_events(WorkItem& item, const SPVTEXTFRAG* frag,
                             bool sentence_events, bool word_events)
{
    if (sentence_events) {
        item.events.push_back({ SPEI_SENTENCE_BOUNDARY, frag->ulTextSrcOffset, frag->ulTextLen });
    }
    if (word_events) {
        const wchar_t* text = frag->pTextStart;
        const ULONG length = frag->ulTextLen;
        bool in_word = false;
        ULONG word_start = 0;
        for (ULONG i = 0; i <= length; ++i) {
            const bool is_word_char = (i < length) &&
                (iswalnum(text[i]) || text[i] == L'\'' || text[i] == L'-');
            if (is_word_char && !in_word) {
                word_start = i;
                in_word = true;
            } else if (!is_word_char && in_word) {
                item.events.push_back({ SPEI_WORD_BOUNDARY,
                                        frag->ulTextSrcOffset + word_start, i - word_start });
                in_word = false;
            }
        }
    }
}

// Split an accumulated body into host-sized renders, preferring sentence
// punctuation, then whitespace.  Returns the cut positions as [start, end).
void split_positions(const std::wstring& body, std::vector<std::pair<size_t, size_t>>& out)
{
    size_t position = 0;
    while (body.size() - position > MAX_SEGMENT_CHARS) {
        const size_t window_end = position + MAX_SEGMENT_CHARS;
        size_t cut = std::wstring::npos;
        for (size_t i = window_end; i > position + 50; --i) {
            const wchar_t ch = body[i - 1];
            if ((ch == L'.' || ch == L'!' || ch == L'?' || ch == L';' || ch == L':') &&
                (i == body.size() || body[i] == L' ')) {
                cut = i;
                break;
            }
        }
        if (cut == std::wstring::npos) {
            for (size_t i = window_end; i > position + 50; --i) {
                if (body[i - 1] == L' ') {
                    cut = i;
                    break;
                }
            }
        }
        if (cut == std::wstring::npos) {
            cut = window_end;
        }
        out.emplace_back(position, cut);
        position = cut;
    }
    if (position < body.size()) {
        out.emplace_back(position, body.size());
    }
}

}

ISpTTSEngineImpl::ISpTTSEngineImpl()
    : voice_index_(0)
{
}

ISpTTSEngineImpl::~ISpTTSEngineImpl() = default;

STDMETHODIMP ISpTTSEngineImpl::SetObjectToken(ISpObjectToken* pToken)
{
    if (!pToken) {
        return E_INVALIDARG;
    }

    try {
        ISpDataKeyPtr attr;
        if (FAILED(pToken->OpenKey(L"Attributes", &attr))) {
            return E_INVALIDARG;
        }

        utils::out_ptr<wchar_t> name(CoTaskMemFree);
        if (FAILED(attr->GetStringValue(L"Name", name.address()))) {
            return E_INVALIDARG;
        }

        voice_index_ = 0;
        for (int i = 0; i < orpheus_voice_count; ++i) {
            if (_wcsicmp(orpheus_voices[i].token_name, name.get()) == 0) {
                voice_index_ = i;
                break;
            }
        }

        token_ = pToken;
        ORPHEUS_LOG("SetObjectToken: voice \"%S\" -> index %d", name.get(), voice_index_);

        // Pre-warm the engine host (and its standby) in the background so the
        // very first utterance does not pay the spawn cost.
        QueueUserWorkItem(
            [](PVOID) -> DWORD {
                OrpheusClient::instance().ensure_ready();
                return 0;
            },
            nullptr, WT_EXECUTEDEFAULT);
        return S_OK;
    }
    catch (const std::bad_alloc&) {
        return E_OUTOFMEMORY;
    }
    catch (...) {
        return E_UNEXPECTED;
    }
}

STDMETHODIMP ISpTTSEngineImpl::GetObjectToken(ISpObjectToken** ppToken)
{
    if (!ppToken) {
        return E_POINTER;
    }
    *ppToken = nullptr;

    if (token_) {
        token_.AddRef();
        *ppToken = token_.GetInterfacePtr();
        return S_OK;
    }
    return E_UNEXPECTED;
}

STDMETHODIMP ISpTTSEngineImpl::GetOutputFormat(
    const GUID* /*pTargetFmtId*/,
    const WAVEFORMATEX* /*pTargetWaveFormatEx*/,
    GUID* pOutputFormatId,
    WAVEFORMATEX** ppCoMemOutputWaveFormatEx)
{
    if (!pOutputFormatId || !ppCoMemOutputWaveFormatEx) {
        return E_POINTER;
    }

    *pOutputFormatId = SPDFID_WaveFormatEx;
    *ppCoMemOutputWaveFormatEx = nullptr;

    auto* pwfex = static_cast<WAVEFORMATEX*>(CoTaskMemAlloc(sizeof(WAVEFORMATEX)));
    if (!pwfex) {
        return E_OUTOFMEMORY;
    }

    pwfex->wFormatTag = WAVE_FORMAT_PCM;
    pwfex->nChannels = AUDIO_CHANNELS;
    pwfex->nSamplesPerSec = AUDIO_SAMPLE_RATE;
    pwfex->wBitsPerSample = AUDIO_BITS_PER_SAMPLE;
    pwfex->nBlockAlign = pwfex->nChannels * pwfex->wBitsPerSample / 8;
    pwfex->nAvgBytesPerSec = pwfex->nSamplesPerSec * pwfex->nBlockAlign;
    pwfex->cbSize = 0;

    *ppCoMemOutputWaveFormatEx = pwfex;
    return S_OK;
}

// Push intonation, head size and voicing into the engine's voice table when
// the configuration utility has changed them.  Only done when settings.ini's
// timestamp moved, because it costs a registry read and, on a real change, a
// restart of every host process.
void ISpTTSEngineImpl::sync_voice_attributes(const voice_attributes& attr,
                                             const settings::VoiceSettings& config)
{
    static FILETIME last_seen = {};
    static CRITICAL_SECTION* guard = [] {
        auto* cs = new CRITICAL_SECTION();
        InitializeCriticalSection(cs);
        return cs;
    }();

    EnterCriticalSection(guard);
    const bool changed = settings::changed_since(last_seen);
    LeaveCriticalSection(guard);
    if (!changed) {
        return;
    }

    voicedesc::Attributes wanted;
    wanted.intonation = config.intonation;
    wanted.head_size = config.head_size;
    wanted.voicing = config.voicing;
    if (voicedesc::matches(attr.get_country(), attr.get_slot(), wanted)) {
        return;
    }
    if (voicedesc::write(attr.get_country(), attr.get_slot(), wanted)) {
        ORPHEUS_LOG("Speak: voice table updated (intonation=%d headSize=%d voicing=%d)",
                    wanted.intonation, wanted.head_size, wanted.voicing);
        OrpheusClient::instance().recycle_all();
    }
}

STDMETHODIMP ISpTTSEngineImpl::Speak(
    DWORD dwSpeakFlags,
    REFGUID /*rguidFormatId*/,
    const WAVEFORMATEX* /*pWaveFormatEx*/,
    const SPVTEXTFRAG* pTextFragList,
    ISpTTSEngineSite* pOutputSite)
{
    if (!pTextFragList || !pOutputSite) {
        return E_INVALIDARG;
    }

    try {
        using namespace protocol;

        const voice_attributes attr(voice_index_);
        const settings::VoiceSettings config =
            settings::load_voice(attr.get_country(), attr.get_slot());
        sync_voice_attributes(attr, config);

        // Pitch 0 in the settings means "keep this voice's own pitch", which
        // is what keeps Synthetic Andy from inheriting Synthetic Dave's.
        int base_pitch = config.pitch;
        if (base_pitch == 0) {
            voicedesc::Attributes stored;
            base_pitch = voicedesc::read(attr.get_country(), attr.get_slot(), stored)
                             ? stored.default_pitch
                             : 100;
        }

        long sapi_rate = 0;
        pOutputSite->GetRate(&sapi_rate);
        unsigned short sapi_volume = 100;
        pOutputSite->GetVolume(&sapi_volume);

        ULONGLONG event_interest = 0;
        pOutputSite->GetEventInterest(&event_interest);
        const bool sentence_events = (event_interest & (1ULL << SPEI_SENTENCE_BOUNDARY)) != 0;
        const bool word_events = (event_interest & (1ULL << SPEI_WORD_BOUNDARY)) != 0;

        ORPHEUS_LOG("Speak: flags=0x%08lX voice=%d (%S) lang=%d slot=%d rate=%ld volume=%u",
                    dwSpeakFlags, voice_index_, attr.entry().token_name,
                    attr.get_lang_index(), attr.get_slot(), sapi_rate, sapi_volume);

        const int default_rate = engine_rate(config.rate, sapi_rate);
        const int default_pitch = engine_pitch(base_pitch, 0);
        const int default_volume = engine_volume(config.volume, sapi_volume, 100);

        // ---- Pass 1: build the work list from the fragment list. ----
        std::vector<WorkItem> items;
        WorkItem current;
        bool current_has_text = false;

        auto note = [&](int id, int value) {
            current.changes.push_back({ current.text.size(), id, value });
        };

        auto flush_segment = [&]() {
            if (!current_has_text) {
                current = WorkItem();
                return;
            }
            std::vector<std::pair<size_t, size_t>> pieces;
            split_positions(current.text, pieces);
            for (size_t i = 0; i < pieces.size(); ++i) {
                const size_t start = pieces[i].first;
                const size_t end = pieces[i].second;
                WorkItem item;
                item.type = WorkItem::Type::Text;
                item.text = current.text.substr(start, end - start);
                if (i == 0) {
                    item.events = std::move(current.events);
                }
                // Carry the parameter state across the cut: anything set at or
                // before `start` still applies to this piece, and is re-emitted
                // at its offset 0.
                for (const ParamChange& change : current.changes) {
                    if (change.offset <= start) {
                        bool replaced = false;
                        for (ParamChange& carried : item.changes) {
                            if (carried.offset == 0 && carried.id == change.id) {
                                carried.value = change.value;
                                replaced = true;
                                break;
                            }
                        }
                        if (!replaced) {
                            item.changes.push_back({ 0, change.id, change.value });
                        }
                    } else if (change.offset < end) {
                        item.changes.push_back({ change.offset - start, change.id, change.value });
                    }
                }
                items.push_back(std::move(item));
            }
            current = WorkItem();
            current_has_text = false;
        };

        for (const SPVTEXTFRAG* frag = pTextFragList; frag; frag = frag->pNext) {
            switch (frag->State.eAction) {
            case SPVA_Bookmark: {
                flush_segment();
                WorkItem item;
                item.type = WorkItem::Type::Bookmark;
                if (frag->ulTextLen > 0 && frag->pTextStart) {
                    item.text.assign(frag->pTextStart, frag->ulTextLen);
                }
                items.push_back(std::move(item));
                break;
            }
            case SPVA_Silence: {
                flush_segment();
                WorkItem item;
                item.type = WorkItem::Type::Silence;
                item.silence_ms = frag->State.SilenceMSecs;
                items.push_back(std::move(item));
                break;
            }
            case SPVA_Speak:
            case SPVA_Pronounce:
            case SPVA_SpellOut: {
                if (frag->ulTextLen == 0 || !frag->pTextStart) {
                    break;
                }
                collect_boundary_events(current, frag, sentence_events, word_events);

                const int frag_rate = engine_rate(config.rate, sapi_rate + frag->State.RateAdj);
                const int frag_pitch = engine_pitch(base_pitch, frag->State.PitchAdj.MiddleAdj);
                const int frag_volume =
                    engine_volume(config.volume, sapi_volume, frag->State.Volume);
                const bool spell = (frag->State.eAction == SPVA_SpellOut);

                if (frag_rate != default_rate)   note(PARAM_RATE, frag_rate);
                if (frag_pitch != default_pitch) note(PARAM_PITCH, frag_pitch);
                if (frag_volume != default_volume) note(PARAM_VOLUME, frag_volume);

                // NVDA's character navigation arrives as SPVA_SpellOut.  The
                // engine's own spelling parameter is not used for it: on a
                // single character it makes no difference to the audio at all
                // (measured), and it cannot rescue the characters the engine
                // renders as silence.  Separating the characters with spaces
                // does the spelling, and silent_char_name does the rest.
                if (spell) {
                    for (ULONG i = 0; i < frag->ulTextLen; ++i) {
                        const wchar_t ch = frag->pTextStart[i];
                        if (const wchar_t* name = silent_char_name(ch)) {
                            current.text += name;
                        } else {
                            append_sanitized(current.text, &ch, 1);
                        }
                        current.text += L' ';
                    }
                } else if (frag->ulTextLen == 1 && silent_char_name(frag->pTextStart[0])) {
                    // A one-character utterance that the engine will not voice.
                    current.text += silent_char_name(frag->pTextStart[0]);
                    current.text += L' ';
                } else {
                    append_sanitized(current.text, frag->pTextStart, frag->ulTextLen);
                    current.text += L' ';
                }

                if (frag_rate != default_rate)   note(PARAM_RATE, default_rate);
                if (frag_pitch != default_pitch) note(PARAM_PITCH, default_pitch);
                if (frag_volume != default_volume) note(PARAM_VOLUME, default_volume);
                current_has_text = true;
                break;
            }
            default:
                break;
            }
        }
        flush_segment();

        // ---- Pass 2: execute. ----
        SpeakContext ctx;
        ctx.site = pOutputSite;

        OrpheusClient& client = OrpheusClient::instance();

        for (WorkItem& item : items) {
            const DWORD actions = pOutputSite->GetActions();
            if (actions & SPVES_ABORT) {
                ORPHEUS_LOG("Speak: abort before item");
                break;
            }
            if (actions & SPVES_SKIP) {
                pOutputSite->CompleteSkip(0);
                break;
            }
            if (actions & SPVES_RATE) {
                pOutputSite->GetRate(&sapi_rate);
            }
            if (actions & SPVES_VOLUME) {
                pOutputSite->GetVolume(&sapi_volume);
            }

            if (item.type == WorkItem::Type::Bookmark) {
                long bookmark_id = 0;
                if (!item.text.empty()) {
                    try {
                        bookmark_id = std::stol(item.text);
                    } catch (...) {
                    }
                }
                SPEVENT event = {};
                event.eEventId = SPEI_TTS_BOOKMARK;
                event.elParamType = SPET_LPARAM_IS_STRING;
                event.ullAudioStreamOffset = ctx.bytes_written;
                event.lParam = reinterpret_cast<LPARAM>(item.text.c_str());
                event.wParam = bookmark_id;
                pOutputSite->AddEvents(&event, 1);
                ORPHEUS_LOG("Speak: bookmark \"%S\" at %llu", item.text.c_str(),
                            ctx.bytes_written);
                continue;
            }

            if (item.type == WorkItem::Type::Silence) {
                const ULONGLONG bytes =
                    static_cast<ULONGLONG>(item.silence_ms) * AUDIO_BYTES_PER_SEC / 1000 & ~1ull;
                std::vector<BYTE> zeros(8192, 0);
                ULONGLONG remaining = bytes;
                while (remaining > 0 && !ctx.aborted) {
                    const uint32_t chunk = static_cast<uint32_t>(
                        (std::min)(remaining, static_cast<ULONGLONG>(zeros.size())));
                    if (!write_audio(ctx, zeros.data(), chunk)) {
                        break;
                    }
                    remaining -= chunk;
                }
                if (ctx.aborted) {
                    break;
                }
                continue;
            }

            // Text segment: fire its boundary events, then render.
            for (const UpfrontEvent& upfront : item.events) {
                SPEVENT event = {};
                event.eEventId = upfront.event_id;
                event.elParamType = SPET_LPARAM_IS_UNDEFINED;
                event.ullAudioStreamOffset = ctx.bytes_written;
                event.lParam = upfront.text_offset;
                event.wParam = upfront.text_length;
                pOutputSite->AddEvents(&event, 1);
            }

            // A leading space gives every parameter at offset 0 a character to
            // attach to before the first word is spoken.
            std::wstring body = L" ";
            body += item.text;
            body += L' ';

            std::vector<EngineParam> params;
            auto add = [&](uint32_t offset, int id, int value) {
                EngineParam p = {};
                p.offset = offset;
                p.type = 0;
                p.id = id;
                p.value = value;
                params.push_back(p);
            };

            add(0, PARAM_LANGUAGE, attr.get_lang_index());
            add(0, PARAM_VOICE, attr.get_slot());
            add(0, PARAM_RATE, engine_rate(config.rate, sapi_rate));
            add(0, PARAM_PITCH, engine_pitch(base_pitch, 0));
            add(0, PARAM_VOLUME, engine_volume(config.volume, sapi_volume, 100));
            add(0, PARAM_SPELLING, config.spelling);
            add(0, PARAM_SKIM, config.skim);
            add(0, PARAM_PAUSE, config.pause);
            add(0, PARAM_WORD_PAUSE, config.word_pause);
            add(0, PARAM_PHRASE_PAUSE, config.phrase_pause);
            add(0, PARAM_BASS_LIFT, config.bass_lift);
            add(0, PARAM_HIGH_LIFT, config.high_lift);
            add(0, PARAM_EXCEPTIONS, config.exceptions);
            add(0, PARAM_ANOMALIES, config.anomalies);

            // Fragment-level overrides; +1 for the leading space.
            for (const ParamChange& change : item.changes) {
                add(static_cast<uint32_t>(change.offset + 1), change.id, change.value);
            }

            bool aborted = false;
            const bool quick_recover = item.text.size() <= QUICK_RECOVER_MAX_CHARS;
            if (!client.speak_segment(body, params, audio_sink, abort_check, &ctx, &aborted,
                                      quick_recover)) {
                ORPHEUS_LOG("Speak: host transport failure");
                return E_FAIL;
            }
            if (ctx.aborted || aborted) {
                break;
            }
        }

        ORPHEUS_LOG("Speak: done, wrote %llu bytes%s", ctx.bytes_written,
                    ctx.aborted ? " (aborted)" : "");
        return S_OK;
    }
    catch (const std::bad_alloc&) {
        return E_OUTOFMEMORY;
    }
    catch (...) {
        return E_UNEXPECTED;
    }
}

}
}
