# CMake platform module for CMAKE_SYSTEM_NAME=PS4 (OpenOrbis).
# The PS4 kernel is FreeBSD 9-derived, so start from CMake's FreeBSD platform
# settings (UNIX, .a/.so naming, -fPIC conventions). Code that needs to tell the
# PS4 apart checks CMAKE_SYSTEM_NAME STREQUAL "PS4" or the __ORBIS__ define.
include(Platform/FreeBSD)
set(PS4 1)
