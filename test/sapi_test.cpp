// End-to-end SAPI test harness for the Orpheus Native SAPI5 engine.
//
// Loads the engine DLL directly, obtains voice tokens from its token
// enumerator and speaks through a real SpVoice.  This runs the full SAPI
// pipeline - XML parsing, format negotiation, engine instantiation through the
// registered CLSID - without requiring administrator rights.
//
// Usage:
//   sapi_test <dll> <outdir>                     speak every voice to a WAV
//   sapi_test <dll> <outdir> --filter <text>     only voices whose name matches
//   sapi_test <dll> <outdir> --text <file>       per-voice text, UTF-8, one
//                                                "<voice index>\t<text>" per line
//   sapi_test <dll> <outdir> --interrupt         cancel-latency scenario
//   sapi_test <dll> <outdir> --spell             spell-out / character checks

#include <sapi.h>
#include <sapiddk.h>
#include <sperror.h>
#include <windows.h>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

typedef HRESULT(STDAPICALLTYPE* DllGetClassObjectFn)(REFCLSID, REFIID, void**);

// CLSID of Orpheus::sapi::IEnumSpObjectTokensImpl.
static const CLSID ENUM_CLSID =
    { 0xe7077968, 0xc442, 0x45ac, { 0xbf, 0x26, 0x9c, 0x5b, 0x36, 0x48, 0xbe, 0x5a } };

static bool check(HRESULT hr, const char* what)
{
    if (FAILED(hr)) {
        printf("FAIL: %s (hr=0x%08lX)\n", what, static_cast<unsigned long>(hr));
        return false;
    }
    return true;
}

static std::wstring utf8_to_wide(const std::string& text)
{
    if (text.empty()) {
        return std::wstring();
    }
    const int needed = MultiByteToWideChar(CP_UTF8, 0, text.data(),
                                           static_cast<int>(text.size()), nullptr, 0);
    std::wstring out(needed, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                        out.data(), needed);
    return out;
}

// "<voice index>\t<utf-8 text>" per line.
static std::map<int, std::wstring> load_text_file(const wchar_t* path)
{
    std::map<int, std::wstring> out;
    FILE* f = _wfopen(path, L"rb");
    if (!f) {
        printf("FAIL: cannot open text file %ls\n", path);
        return out;
    }
    std::string data;
    char buffer[4096];
    size_t n = 0;
    while ((n = fread(buffer, 1, sizeof(buffer), f)) > 0) {
        data.append(buffer, n);
    }
    fclose(f);

    size_t start = 0;
    while (start <= data.size()) {
        size_t end = data.find('\n', start);
        if (end == std::string::npos) {
            end = data.size();
        }
        std::string line = data.substr(start, end - start);
        start = end + 1;
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) {
            line.pop_back();
        }
        const size_t tab = line.find('\t');
        if (tab == std::string::npos) {
            continue;
        }
        const int index = atoi(line.substr(0, tab).c_str());
        out[index] = utf8_to_wide(line.substr(tab + 1));
        if (end == data.size()) {
            break;
        }
    }
    return out;
}

static std::wstring token_name(ISpObjectToken* token)
{
    std::wstring name = L"voice";
    ISpDataKey* attributes = nullptr;
    if (SUCCEEDED(token->OpenKey(L"Attributes", &attributes))) {
        LPWSTR value = nullptr;
        if (SUCCEEDED(attributes->GetStringValue(L"Name", &value))) {
            name = value;
            CoTaskMemFree(value);
        }
        attributes->Release();
    }
    return name;
}

// Speak `text` with `token` into a WAV file; returns the file size, or -1.
static LONGLONG speak_to_wav(ISpObjectToken* token, const std::wstring& text,
                             const std::wstring& wav_path, ULONGLONG* elapsed_out)
{
    ISpVoice* voice = nullptr;
    if (!check(CoCreateInstance(CLSID_SpVoice, nullptr, CLSCTX_INPROC_SERVER, IID_ISpVoice,
                                reinterpret_cast<void**>(&voice)),
               "create SpVoice")) {
        return -1;
    }
    if (!check(voice->SetVoice(token), "SetVoice")) {
        voice->Release();
        return -1;
    }

    ISpStream* stream = nullptr;
    if (!check(CoCreateInstance(CLSID_SpStream, nullptr, CLSCTX_INPROC_SERVER, IID_ISpStream,
                                reinterpret_cast<void**>(&stream)),
               "create SpStream")) {
        voice->Release();
        return -1;
    }

    WAVEFORMATEX wfx = {};
    wfx.wFormatTag = WAVE_FORMAT_PCM;
    wfx.nChannels = 1;
    wfx.nSamplesPerSec = 22050;
    wfx.wBitsPerSample = 16;
    wfx.nBlockAlign = 2;
    wfx.nAvgBytesPerSec = 44100;

    GUID format_id = SPDFID_WaveFormatEx;
    if (!check(stream->BindToFile(wav_path.c_str(), SPFM_CREATE_ALWAYS, &format_id, &wfx, 0),
               "BindToFile")) {
        stream->Release();
        voice->Release();
        return -1;
    }

    voice->SetOutput(stream, TRUE);
    const ULONGLONG t0 = GetTickCount64();
    const HRESULT hr = voice->Speak(text.c_str(), SPF_DEFAULT | SPF_IS_NOT_XML, nullptr);
    if (elapsed_out) {
        *elapsed_out = GetTickCount64() - t0;
    }
    stream->Close();
    stream->Release();
    voice->Release();
    if (FAILED(hr)) {
        check(hr, "Speak");
        return -1;
    }

    WIN32_FILE_ATTRIBUTE_DATA info = {};
    if (GetFileAttributesExW(wav_path.c_str(), GetFileExInfoStandard, &info)) {
        return (static_cast<LONGLONG>(info.nFileSizeHigh) << 32) | info.nFileSizeLow;
    }
    return 0;
}

int wmain(int argc, wchar_t** argv)
{
    if (argc < 3) {
        printf("usage: sapi_test <dll> <outdir> [--filter <text>] [--text <file>]"
               " [--interrupt] [--spell]\n");
        return 1;
    }
    setvbuf(stdout, nullptr, _IONBF, 0);
    const wchar_t* dll_path = argv[1];
    const std::wstring out_dir = argv[2];

    const wchar_t* filter = nullptr;
    const wchar_t* text_file = nullptr;
    bool interrupt_mode = false;
    bool spell_mode = false;
    for (int i = 3; i < argc; ++i) {
        if (wcscmp(argv[i], L"--filter") == 0 && i + 1 < argc) {
            filter = argv[++i];
        } else if (wcscmp(argv[i], L"--text") == 0 && i + 1 < argc) {
            text_file = argv[++i];
        } else if (wcscmp(argv[i], L"--interrupt") == 0) {
            interrupt_mode = true;
        } else if (wcscmp(argv[i], L"--spell") == 0) {
            spell_mode = true;
        }
    }

    if (!check(CoInitialize(nullptr), "CoInitialize")) {
        return 1;
    }

    HMODULE dll = LoadLibraryW(dll_path);
    if (!dll) {
        printf("FAIL: LoadLibrary %ls (error %lu)\n", dll_path, GetLastError());
        return 1;
    }
    auto get_class_object =
        reinterpret_cast<DllGetClassObjectFn>(GetProcAddress(dll, "DllGetClassObject"));
    if (!get_class_object) {
        printf("FAIL: DllGetClassObject export missing\n");
        return 1;
    }

    IClassFactory* factory = nullptr;
    if (!check(get_class_object(ENUM_CLSID, IID_IClassFactory,
                                reinterpret_cast<void**>(&factory)),
               "get enumerator class object")) {
        return 1;
    }
    IEnumSpObjectTokens* tokens = nullptr;
    if (!check(factory->CreateInstance(nullptr, __uuidof(IEnumSpObjectTokens),
                                       reinterpret_cast<void**>(&tokens)),
               "create enumerator")) {
        return 1;
    }
    factory->Release();

    ULONG count = 0;
    tokens->GetCount(&count);
    printf("voice tokens: %lu\n", count);

    std::map<int, std::wstring> per_voice_text;
    if (text_file) {
        per_voice_text = load_text_file(text_file);
        printf("loaded text for %d voices\n", static_cast<int>(per_voice_text.size()));
    }

    if (interrupt_mode) {
        // Cancel latency: speak a long utterance to the audio device, purge it
        // mid-render, and time how quickly the next utterance completes.
        ISpObjectToken* token = nullptr;
        if (!check(tokens->Item(20, &token), "Item(20)")) { // UK English, Synthetic Dave
            return 1;
        }
        ISpVoice* voice = nullptr;
        if (!check(CoCreateInstance(CLSID_SpVoice, nullptr, CLSCTX_INPROC_SERVER, IID_ISpVoice,
                                    reinterpret_cast<void**>(&voice)),
                   "create SpVoice")) {
            return 1;
        }
        if (!check(voice->SetVoice(token), "SetVoice")) {
            return 1;
        }
        token->Release();

        std::wstring long_text;
        for (int i = 0; i < 8; ++i) {
            long_text += L"The quick brown fox jumps over the lazy dog near the river bank. ";
        }
        for (int trial = 0; trial < 4; ++trial) {
            voice->Speak(long_text.c_str(),
                         SPF_ASYNC | SPF_PURGEBEFORESPEAK | SPF_IS_NOT_XML, nullptr);
            Sleep(400);
            const ULONGLONG t0 = GetTickCount64();
            voice->Speak(L"Next line.",
                         SPF_ASYNC | SPF_PURGEBEFORESPEAK | SPF_IS_NOT_XML, nullptr);
            voice->WaitUntilDone(15000);
            printf("trial %d: short line done %llu ms after purge\n", trial,
                   GetTickCount64() - t0);
            Sleep(300);
        }
        voice->Release();
        tokens->Release();
        CoUninitialize();
        printf("RESULT: interrupt scenario complete\n");
        return 0;
    }

    if (spell_mode) {
        // Character navigation arrives as <spell> XML.  Every printable
        // character must produce audio, and different characters must produce
        // different audio - a lone capital read as a Roman numeral, or a
        // character that renders silence, is a real defect for a screen
        // reader user.
        ISpObjectToken* token = nullptr;
        if (!check(tokens->Item(20, &token), "Item(20)")) {
            return 1;
        }
        const wchar_t* chars = L"abcxyzCIVXLM0123456789.,;:!?@#$%&*()-+=/";
        std::map<std::wstring, LONGLONG> sizes;
        int failures = 0;
        for (const wchar_t* p = chars; *p; ++p) {
            wchar_t xml[64];
            swprintf_s(xml, L"<spell>%c</spell>", *p);
            // The output directory can be long; a fixed buffer here makes
            // swprintf_s fail-fast the whole process rather than truncate.
            wchar_t leaf[32];
            swprintf_s(leaf, L"\\spell_%04X.wav", static_cast<unsigned>(*p));
            const std::wstring name_str = out_dir + leaf;
            const wchar_t* name = name_str.c_str();
            ISpVoice* voice = nullptr;
            if (FAILED(CoCreateInstance(CLSID_SpVoice, nullptr, CLSCTX_INPROC_SERVER,
                                        IID_ISpVoice, reinterpret_cast<void**>(&voice)))) {
                ++failures;
                continue;
            }
            voice->SetVoice(token);
            ISpStream* stream = nullptr;
            CoCreateInstance(CLSID_SpStream, nullptr, CLSCTX_INPROC_SERVER, IID_ISpStream,
                             reinterpret_cast<void**>(&stream));
            WAVEFORMATEX wfx = { WAVE_FORMAT_PCM, 1, 22050, 44100, 2, 16, 0 };
            GUID fid = SPDFID_WaveFormatEx;
            stream->BindToFile(name, SPFM_CREATE_ALWAYS, &fid, &wfx, 0);
            voice->SetOutput(stream, TRUE);
            voice->Speak(xml, SPF_DEFAULT, nullptr);
            stream->Close();
            stream->Release();
            voice->Release();

            WIN32_FILE_ATTRIBUTE_DATA info = {};
            LONGLONG size = 0;
            if (GetFileAttributesExW(name, GetFileExInfoStandard, &info)) {
                size = (static_cast<LONGLONG>(info.nFileSizeHigh) << 32) | info.nFileSizeLow;
            }
            printf("  '%lc' -> %lld bytes\n", *p, size);
            if (size < 2000) {
                printf("    FAIL: silent or near-silent\n");
                ++failures;
            }
            sizes[std::wstring(1, *p)] = size;
        }
        token->Release();
        tokens->Release();
        CoUninitialize();
        printf(failures ? "RESULT: %d spell failure(s)\n" : "RESULT: all characters spoke\n",
               failures);
        return failures ? 1 : 0;
    }

    int failures = 0;
    int spoken = 0;
    for (ULONG i = 0; i < count; ++i) {
        ISpObjectToken* token = nullptr;
        if (!check(tokens->Item(i, &token), "enumerator Item")) {
            ++failures;
            continue;
        }
        const std::wstring name = token_name(token);
        if (filter && name.find(filter) == std::wstring::npos) {
            token->Release();
            continue;
        }
        printf("[%2lu] %-46ls ", i, name.c_str());

        std::wstring text;
        auto it = per_voice_text.find(static_cast<int>(i));
        if (it != per_voice_text.end()) {
            text = it->second;
        } else {
            text = L"Hello. You are listening to " + name +
                   L", speaking through Microsoft Speech A P I five.";
        }

        std::wstring safe_name;
        for (wchar_t ch : name) {
            safe_name += (iswalnum(ch) || ch == L'-') ? ch : L'_';
        }
        wchar_t prefix[8];
        swprintf_s(prefix, L"%02lu_", i);
        const std::wstring wav_path = out_dir + L"\\" + prefix + safe_name + L".wav";

        ULONGLONG elapsed = 0;
        const LONGLONG size = speak_to_wav(token, text, wav_path, &elapsed);
        token->Release();

        if (size < 0) {
            ++failures;
            printf("FAILED\n");
            continue;
        }
        ++spoken;
        printf("%8lld bytes  %4llu ms", size, elapsed);
        if (size < 20000) {
            printf("   WARNING: suspiciously small");
            ++failures;
        }
        printf("\n");
    }

    tokens->Release();
    CoUninitialize();
    printf("spoke %d of %lu voices, %d failure(s)\n", spoken, count, failures);
    printf(failures ? "RESULT: FAILED\n" : "RESULT: all passed\n");
    return failures ? 1 : 0;
}
