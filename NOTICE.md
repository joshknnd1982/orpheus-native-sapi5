# Notices

The code written for this project is licensed under the MIT License (see [LICENSE](LICENSE)). The material
below is not covered by that licence and stays under its own terms.

## Scope, and the Dolphin Orpheus speech engine

The MIT licence above covers the work created in this repository: the SAPI 5
interface and engine client (src/), the test harnesses (test/), the
configuration utility, the build files and the Inno Setup script (installer/).

It does NOT cover the Dolphin Orpheus speech engine or its voice and language
data - the contents of bin/orpheus/ and bin/orpheus-native-host.exe. Those
remain the property of Dolphin Computer Access. They are redistributed here
unmodified so that an abandoned synthesiser keeps working for the people who
rely on it, and are not licensed under the terms above.

## Not covered: the SAPI 5 COM skeleton from the BestSpeech SAPI 5 wrapper

The files below are an exception to the scope statement above, which says the MIT licence
covers the SAPI 5 interface and engine client (src/). They were adapted from the SAPI 5 COM
server and token enumerator skeleton of the BestSpeech SAPI 5 wrapper by Gozaltech
(<https://github.com/gozaltech/BstSpeech-sapi>), they are not the work of this project's
author, and the MIT License does not cover them. They stay under their original author's terms.

- `src/com.hpp` and `src/com.cpp`
- `src/registry.hpp` and `src/registry.cpp`
- `src/utils.hpp`
- `src/ISpDataKeyImpl.hpp` and `src/ISpDataKeyImpl.cpp`
- `src/IEnumSpObjectTokensImpl.hpp` and `src/IEnumSpObjectTokensImpl.cpp`
- `src/voice_token.hpp` and `src/voice_token.cpp`

## Not covered: the NVDA add-on reference copy

The files in `reference/nvda-addon/` are the Python driver from the NVDA add-on this project
started from, and that add-on's default dictionary (see [reference/README.md](reference/README.md)).
They are kept for provenance, they are not code written for this project, and the MIT License
does not cover them.
