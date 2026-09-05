# Orpheus Native SAPI5

A native Windows SAPI 5 interface for **Dolphin Orpheus 2.10**, the formant
speech synthesiser from Dolphin Computer Access. It exposes all 25 languages
and all 48 voices to any SAPI5 application — NVDA, JAWS, Narrator, Balabolka,
Bookworm, Windows Speech — in both 32-bit and 64-bit processes.

The engine is abandonware. This project does not change it: it drives the
original 32-bit engine binaries directly and translates between SAPI 5 and the
engine's own parameter protocol.

**No SAPI 4. No Dolphin Synthesizer Access Manager. No Python. No registry
setup.**

---

## Download

Get `OrpheusNativeSAPI_Setup_1.1.0.exe` from the
[Releases](../../releases) page.

The installer places the 32-bit and 64-bit interfaces, the engine, the
languages you choose and the configuration utility, and registers the voices
with SAPI. It needs administrator rights because SAPI's voice list is
machine-wide.

### Choosing what gets installed

The wizard's component page offers three setup types — **Full** (all 25
languages, all 48 voices), **English only** (US and UK English, four voices),
and **Custom**. Under Custom every language is a separate tick, and each
language's *second* voice is a tick of its own beneath it, so you can take
German with both Klaus and Andreas but Welsh with David alone. Setup will not
let you continue with no language selected.

Only the voices you chose appear in the Windows voice list and in the
configuration utility — a voice whose data files are absent is never offered,
so it cannot be selected and then fail. Re-running the installer lets you
change the selection.

Your choice is recorded in `voices.ini` in the installation folder, which the
SAPI interfaces and the configuration utility read.

Windows SmartScreen will warn about an unrecognised publisher. That is a
reputation signal on a newly released, unsigned binary, not a malware finding.

---

## The voices

25 languages. Every language ships two voices except Chinese Putonghua and
Cantonese, which ship one — their second `.vcx` files are present but the
engine refuses to load them and clamps back to the first voice, which was
confirmed by rendering both slots and comparing the audio byte for byte.

| Language | Locale | Voices |
|---|---|---|
| US English | en-US | Synthetic Dave, Synthetic Andy |
| Greek | el-GR | Synthetic Dave, Synthetic Andy |
| Dutch | nl-NL | Jan, Hendrick |
| French | fr-FR | Jean, Pierre |
| Castilian Spanish | es-ES | David, Andrés |
| Hungarian | hu-HU | Istvan, Marcus |
| Croatian | hr-HR | Stjepan, Marija |
| Italian | it-IT | Davide, Andrea |
| Romanian | ro-RO | David, Andrei |
| Czech | cs-CZ | Honza, Katka |
| UK English | en-GB | Synthetic Dave, Synthetic Andy |
| Danish | da-DK | Thomas, Lasse |
| Swedish | sv-SE | Tomas, Lasse |
| Norwegian | nb-NO | Knut, Andreas |
| Polish | pl-PL | Synthetic Dave, Synthetic Andy |
| German | de-DE | Klaus, Andreas |
| Latin American Spanish | es-MX | David, Andrés |
| Brazilian Portuguese | pt-BR | João, Isabel |
| Malay | ms-MY | David, Anne |
| Chinese Putonghua | zh-CN | Dave |
| Portuguese | pt-PT | João, Isabel |
| Finnish | fi-FI | Dave, Andy |
| Lithuanian | lt-LT | Jonas, Petras |
| Welsh | cy-GB | David, Megan |
| Cantonese | zh-HK | John |

Voices appear in SAPI as `Orpheus <Language> - <Voice>`, for example
`Orpheus UK English - Synthetic Dave`. Each carries its language as a SAPI
`Language` attribute, so applications that pick a voice by locale find the
right one.

Audio is 22050 Hz, 16-bit, mono.

---

## Parameters

The engine reports 17 parameters. Every one below was tested by rendering the
same sentence at its low and high values and comparing the audio, so the
"works" column is measured, not assumed.

### Sent with every utterance

| Parameter | Range | Default | Notes |
|---|---|---|---|
| Rate | 10–700 wpm | 160 | SAPI's rate (−10…+10) sweeps the range around this |
| Pitch | 50–500 Hz | per voice | 0 keeps the voice's own pitch — see below |
| Volume | 0–100 % | 100 | multiplied by the SAPI volume |
| Spelling level | 0–15 | 0 | 0 is off |
| Skim reading level | 0–8 | 0 | |
| General pause | 0–100 | 10 | |
| Word pause | 0–1000 ms | 0 | |
| Phrase pause | 0–2000 ms | 250 | |
| Bass lift | −100–100 % | 0 | |
| High lift | −100–100 % | 0 | |
| Pronunciation exceptions | on/off | on | uses `orpheus\settings\*.exc` |
| Anomalies | on/off | on | exposed for completeness; no audible effect was found |

Two further engine parameters are used internally and are not user settings:
**Language** and **Voice** select the voice, and **Index** carries the
end-of-utterance marker.

**Balance** (−100…100) exists in the engine and is deliberately *not* exposed:
the engine renders a single channel, so it has nothing to act on. Setting it
across its whole range produces byte-identical audio.

**Sound effect index** (0–30, the beeps in `orpheus\sfx`) is likewise not a
speech setting and is not exposed.

### Held per voice, in the engine's own voice table

| Parameter | Range | Default |
|---|---|---|
| Intonation | 0–100 | per voice, usually 50 |
| Head size | −100–100 | per voice, usually 0 |
| Voicing | 0–100 | per voice, usually 100 |

These three are not engine parameters — there is no id for them in the
protocol. They live in a binary blob the engine keeps in the registry and
reads when a synthesis process starts, so changing one restarts the engine
processes. That takes a moment longer than the other settings; everything else
applies to the very next utterance.

The defaults in that table are **per voice**, not global: Chinese Putonghua and
Cantonese ship with intonation 80 rather than 50, and the second Czech and
Malay voices with voicing 90 rather than 100. So nothing is written to the
voice table until you actually change one of these three — installing this
wrapper does not retune voices you never touched. Until then the configuration
utility shows the value the engine itself holds for that voice.

### Pitch, and why 0 means something

Each voice carries its own default pitch: Synthetic Dave is 100 Hz, Synthetic
Andy is 135 Hz. Leaving the pitch setting at **0** keeps that per-voice
value, so the two voices in a language stay distinct. Setting an explicit
pitch overrides it for that voice and flattens the difference.

---

## The configuration utility

`OrpheusNativeConfig.exe` — on the desktop and in the Start menu after
installation.

Pick a voice from the drop-down list and every setting below it applies to
that voice. The list holds only the voices that were installed. Changes are
written as you make them and are picked up on the next utterance, including
while a screen reader is speaking. **Apply to all voices** copies the current
settings across every installed voice.

Every control is labelled, is a tab stop and has its own access key; the
dialog is verified after each build by `a11y_probe`, which reads the live
window back through MSAA and fails the build on an unnamed control or a
duplicated access key. Numeric fields are edit boxes with spin buttons rather
than sliders on purpose: MSAA reports a slider's position as a percentage of
its range, so a 0-to-8 slider sitting on 5 gets announced as "62".

Settings live in `%APPDATA%\OrpheusNativeSAPI\settings.ini`, shared by the
32-bit interface, the 64-bit interface and the utility.

---

## How it works

```
SAPI application (32- or 64-bit)
        |
        v
OrpheusNativeSAPI.dll          ISpTTSEngine + a token enumerator
        |  loopback TCP, framed
        v
orpheus-native-host.exe        32-bit, ships with the engine
        |
        v
a3s.dll                        the Orpheus 2.10 synthesiser
```

Both interfaces are built from the same source. The engine is 32-bit only, so
the 64-bit interface talks to the same 32-bit host over a loopback socket
rather than needing a separate bridge — the socket *is* the architecture
boundary. Renders from the two are byte-identical.

One engine quirk worth knowing if you ever compare recordings: **a freshly
started engine process renders the same text bit-for-bit identically every
time, but a process that has already spoken does not.** The engine carries
state across utterances, so the second and third rendering of the same
sentence differ slightly from the first and from each other. Nothing is wrong
when that happens; it is not a fault in the wrapper, and it is why the first
utterance from a newly spawned host is a few milliseconds longer.

**Cancelling never waits for the engine.** The engine's mute command can take
seconds to settle mid-render, so an interrupted render is discarded rather
than drained: the client keeps two pre-warmed standby hosts and promotes one
immediately. Measured through real SAPI, abort to standby promotion is about
1 ms and abort to the first audio of the next utterance about 28 ms. Short
utterances — the single characters a screen reader user arrows through — are
parked to finish quietly and rejoin the pool instead of being killed.

Utterances are split at bookmarks and sentence boundaries so bookmark events
carry accurate offsets and cancellation stays responsive.

### Characters that the engine will not say

Eighteen ASCII characters render as silence in this engine, in both normal and
spelling mode:

```
@ # % & * - + = / _ < > \ | ~ ^ " `
```

Inside a sentence that is correct — a hyphen should not be announced. When the
character *is* the utterance it is not: a screen reader user arrowing across
`a + b` would hear nothing at all for the plus. So when a lone character is
being read out, one the engine will not voice is replaced by its name ("plus",
"slash", "at"). Screen readers that substitute their own symbol names still
win, because those arrive as ordinary words.

---

## What it does not depend on

**SAPI 4.** The engine is driven directly through its own protocol.

**The Dolphin Synthesizer Access Manager.** `sam\dolosam.dll` and its `Dso*.ini`
files are not installed and not used. All 48 voices render byte-identically
without them.

**Python.** The NVDA add-on this work started from is Python; none of it
ships. Everything here is C++ and the engine's own native binaries.

**The registry, for the engine.** Setup writes no engine configuration. The
engine builds its own voice table from the `Language\` folders the first time
it runs — verified by deleting the entire `HKCU\Software\Dolphin` branch and
watching all 25 languages and 48 voices come back. The only registry work
Setup does is the SAPI registration every SAPI5 voice needs.

The one thing that *is* installed writable is `orpheus\orpheus.sys`, an
804-byte file the engine stamps its own directory into every time it starts.
It refuses to initialise if it cannot write there, and under Program Files
that fails for a standard user, so that single file is granted modify rights.
Nothing else in the tree is writable — the engine DLLs are not.

---

## Uninstalling

SAPI 5's voice list is one shared, machine-wide registry key. An uninstall
that leaves a broken entry there does not merely leave a mess of its own: it
hands every other speech engine on the machine a voice that cannot be created,
and clients that remember their voice by token path — NVDA does — can then
fail to start SAPI5 at all.

So the registration is removed three independent ways, and none can take the
others down with it:

1. `DllUnregisterServer`, through Setup's `regserver` flags.
2. `[Registry]` entries flagged `uninsdeletekey dontcreatekey`, so Setup
   records the deletions without creating anything — these work even if
   `regsvr32` never ran.
3. A sweep at uninstall that deletes the keys directly in both the 32-bit and
   64-bit registry views, and clears the default-voice pointer if it names one
   of these voices.

No parent key is ever touched: only this project's own `TokenEnums` subkey and
its own two CLSIDs.

The specific trap being avoided is `RegDeleteKeyW`, which refuses any key that
still has subkeys and returns `ERROR_ACCESS_DENIED`. Every SAPI voice token
has an `Attributes` subkey, so a delete written that way silently removes
nothing. `test/registry_test.cpp` drives the real registration and removal
functions against a sandbox key under `HKCU` and asserts, among other things,
that another vendor's enumerator and default voice survive the sweep. It runs
as part of every build.

A full install-then-uninstall was checked against a snapshot of the machine's
entire SAPI state — every voice, every CLSID, both registry views — and came
back identical to the pre-install baseline.

Per-voice settings and debug logs in `%APPDATA%\OrpheusNativeSAPI` are
deliberately left in place. Setup runs elevated, so that path would resolve to
the administrator's profile rather than the profile that actually holds them;
deleting the wrong user's data is worse than leaving a few kilobytes of your
own. Remove that folder by hand if you want it gone.

---

## Logging

Both interfaces and the configuration utility write to
`%APPDATA%\OrpheusNativeSAPI\logs\<process>_<arch>_<pid>.log`. Logging is on
by default and can be turned off in the configuration utility.

The installer writes a detailed setup log and copies it to
`<install folder>\logs\install.log`.

---

## Building from source

```batch
build_all.bat
```

Requirements: Windows 10 or later, Visual Studio 2022 Build Tools with the C++
workload, CMake 3.15+, and Inno Setup 6 for the installer.

The build produces both architectures, runs the registration test in both, and
compiles the installer. It fails rather than packaging if the uninstall test
does not pass.

### Test harnesses

| Harness | What it checks |
|---|---|
| `registry_test` | the real registration and removal code, in an HKCU sandbox |
| `sapi_test` | speaks every voice through real SAPI to WAV files |
| `sapi_test --spell` | every character produces audio, and different characters differ |
| `sapi_test --interrupt` | cancel latency against the audio device |
| `a11y_probe` | reads the configuration dialog back through MSAA |

---

## Repository layout

```
src/          the SAPI5 interface, engine client and configuration utility
test/         the harnesses above
bin/          the Orpheus engine and its host, exactly as installed
dist/         prebuilt 32-bit and 64-bit interfaces and the config utility
installer/    the Inno Setup script
```

Clone the repository and you have the engine and prebuilt binaries as well as
the source. The installer itself is on the Releases page.

---

## Credits

The Orpheus synthesiser is the work of Dolphin Computer Access. This project
is an independent SAPI 5 interface for it and is not affiliated with or
endorsed by Dolphin.

The engine host and the original driver came from an NVDA add-on; the SAPI5
interface, configuration utility and installer here are new.

## License

The SAPI5 interface, configuration utility, test harnesses and installer
script in this repository are open source. The Orpheus engine binaries and
voice data remain the property of Dolphin Computer Access and are included
here only so that the abandoned synthesiser keeps working.
