#pragma once

// The single source of truth for the version number.
//
// build_all.bat reads ORPHEUS_VERSION_STRING back out of this file to name the
// installer, and version.rc stamps it into every binary's file properties, so
// a user can always tell which build they are running. Keep the four-part
// number, the string, installer/orpheus_native.iss's MyAppVersion, the
// project() line in CMakeLists.txt and the README's version in step.

#define ORPHEUS_VERSION_MAJOR 1
#define ORPHEUS_VERSION_MINOR 1
#define ORPHEUS_VERSION_PATCH 0
#define ORPHEUS_VERSION_BUILD 0

#define ORPHEUS_VERSION_STRING "1.1.0"
#define ORPHEUS_VERSION_STRING_W L"1.1.0"
