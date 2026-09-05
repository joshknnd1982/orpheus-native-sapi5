# Reference material

Nothing in this folder ships, is built, or is installed.

`nvda-addon/` holds the Python driver from the NVDA add-on this project
started from. It is kept for provenance and because it documents the engine's
framed-TCP protocol in a readable form. The SAPI 5 interface is a fresh C++
implementation and does not use it — the installed product contains no Python
at all.

Two things in the add-on turned out to be wrong or unnecessary, and are worth
recording:

- It scaled volume in software, on the grounds that the engine had no reliable
  volume parameter. The engine's own `Volume` parameter works; this project
  uses it.
- It located a voice's record in the engine's voice table by counting matching
  entries in list order. This project matches on the stored file path instead,
  which is stable regardless of how the table happens to be ordered.
