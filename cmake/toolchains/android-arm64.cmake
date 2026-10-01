# cmake/toolchains/android-arm64.cmake
#
# CMake toolchain for cross-compiling Ultralight-WebBrowser to Android ARM64
# (aarch64-linux-android) with the Android NDK.
#
# This is a thin, validated wrapper around the NDK's own
# `build/cmake/android.toolchain.cmake`. Delegating to the NDK toolchain is
# deliberate: it owns sysroot selection, the Clang target triple, the API-level
# sysroot and the NDK rlib handling, all of which are version-specific and easy
# to get subtly wrong by hand.
#
# Usage:
#   cmake -S . -B build-android \
#     -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/android-arm64.cmake \
#     -DANDROID_NDK=$ANDROID_NDK \
#     -DANDROID_API=24 \
#     -DULTRALIGHT_SDK_ROOT=$SDK_ROOT
#   cmake --build build-android --parallel
#
# `ANDROID_NDK` may also come from the environment. API 24 is the minimum
# supported by the current Ultralight Android SDKs and by ARMv8-A baseline
# instructions; override with -DANDROID_API=<level> if you need newer.

if(NOT DEFINED ANDROID_NDK OR ANDROID_NDK STREQUAL "")
	if(DEFINED ENV{ANDROID_NDK} AND NOT "$ENV{ANDROID_NDK}" STREQUAL "")
		set(ANDROID_NDK "$ENV{ANDROID_NDK}" CACHE PATH "Path to the Android NDK" FORCE)
	else()
		message(FATAL_ERROR
			"ANDROID_NDK is not set. Pass -DANDROID_NDK=/path/to/ndk or export ANDROID_NDK.")
	endif()
endif()

# Validate before normalizing. A relative path here almost always means
# ANDROID_NDK would otherwise be resolved against the wrong base directory,
# which the NDK toolchain then reports cryptically.
if(NOT IS_ABSOLUTE "${ANDROID_NDK}")
	message(FATAL_ERROR
		"ANDROID_NDK must be an absolute path (got '${ANDROID_NDK}').")
endif()

get_filename_component(ANDROID_NDK "${ANDROID_NDK}" ABSOLUTE)

if(NOT IS_DIRECTORY "${ANDROID_NDK}")
	message(FATAL_ERROR "ANDROID_NDK ('${ANDROID_NDK}') is not a directory.")
endif()

# Newer NDKs (r23+) ship the CMake toolchain; older ones are not supported
# because they lack the unified sysroot/Clang setup this project relies on.
set(_ultralight_ndk_toolchain "${ANDROID_NDK}/build/cmake/android.toolchain.cmake")
if(NOT EXISTS "${_ultralight_ndk_toolchain}")
	message(FATAL_ERROR
		"Could not find '${_ultralight_ndk_toolchain}'. "
		"Install Android NDK r23 or newer and point ANDROID_NDK at its root.")
endif()

# API 24 is the minimum supported by the current Ultralight Android SDKs and by
# the ARMv8-A baseline instruction set. Callers may raise it with
# -DANDROID_API=<level>; a bare number or a full "android-<level>" is accepted.
if(NOT DEFINED ANDROID_API OR ANDROID_API STREQUAL "")
	set(ANDROID_API "24")
endif()
if(ANDROID_API MATCHES "^[0-9]+$")
	set(ANDROID_PLATFORM "android-${ANDROID_API}" CACHE STRING "Android API level to build against" FORCE)
elseif(ANDROID_API MATCHES "^android-([0-9]+)$")
	set(ANDROID_PLATFORM "android-${CMAKE_MATCH_1}" CACHE STRING "Android API level to build against" FORCE)
else()
	message(FATAL_ERROR
		"ANDROID_API must be an API level number (eg. 24) or 'android-<level>'; got '${ANDROID_API}'.")
endif()
if(ANDROID_PLATFORM STREQUAL "android-24")
	# No warning: 24 is the supported default.
elseif(ANDROID_API LESS 24)
	message(FATAL_ERROR
		"ANDROID_API ${ANDROID_API} is below the minimum supported level of 24.")
endif()

set(ANDROID_ABI "arm64-v8a" CACHE STRING "Android ABI to build for" FORCE)

include("${_ultralight_ndk_toolchain}")

# Cross-compiled binaries cannot be executed by the host, so disable anything
# that would try to run them. Tests are also skipped in CMakeLists.txt for the
# same reason.
set(CMAKE_CROSSCOMPILING ON)
set(CMAKE_SYSTEM_NAME Android)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

# The NDK Clang wrapper already encodes the right flags, so avoid CMake probing
# for link capabilities that would produce a spurious failure.
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
