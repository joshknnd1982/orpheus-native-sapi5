#pragma once

// Shared-file diagnostic logging for the Orpheus Native SAPI5 components.
// One log file per process under %APPDATA%\OrpheusNativeSAPI\logs.  Enabled
// by default; the configuration utility can turn it off (settings.ini,
// [diagnostics] logging=0).  The file is opened with _SH_DENYNO and plain "a"
// mode: text-mode ccs= encodings make fprintf fail-fast the whole process.

namespace Orpheus {
namespace logging {

void log(const char* format, ...);

}
}

#define ORPHEUS_LOG(...) ::Orpheus::logging::log(__VA_ARGS__)
