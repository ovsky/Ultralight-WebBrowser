cmake_minimum_required(VERSION 3.8)
if(APPLE)
	project(UltralightWebBrowser LANGUAGES CXX OBJCXX)
	set(CMAKE_OBJCXX_STANDARD 17)
	set(CMAKE_OBJCXX_STANDARD_REQUIRED ON)
else()
	project(UltralightWebBrowser LANGUAGES CXX)
endif()
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(CREATE_INSTALLER "Create a Windows installer via CPack (NSIS)" OFF)

include(CTest)

# Ultralight SDK: default to repository `data/` folder when not set
set(ULTRALIGHT_SDK_ROOT "" CACHE PATH "Path to Ultralight SDK root (contains include, lib, bin)")
if(NOT ULTRALIGHT_SDK_ROOT)
	set(ULTRALIGHT_SDK_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/data")
endif()

if(NOT EXISTS "${ULTRALIGHT_SDK_ROOT}/include" OR NOT EXISTS "${ULTRALIGHT_SDK_ROOT}/lib")
	message(FATAL_ERROR "ULTRALIGHT_SDK_ROOT (\"${ULTRALIGHT_SDK_ROOT}\") does not look valid. Expect 'include' and 'lib' subfolders.")
endif()

include_directories(
	"${CMAKE_CURRENT_SOURCE_DIR}"
	"${CMAKE_CURRENT_SOURCE_DIR}/src"
	"${ULTRALIGHT_SDK_ROOT}/include"
)

function(add_app NAME)
	if (WIN32)
		add_executable(${NAME} WIN32 ${ARGN})
	else()
		add_executable(${NAME} ${ARGN})
	endif()

	if(MSVC)
		target_compile_options(${NAME} PRIVATE /GR-)
	else()
		target_compile_options(${NAME} PRIVATE -fno-rtti)
	endif()

	find_library(APP_CORE_LIB NAMES AppCore PATHS "${ULTRALIGHT_SDK_ROOT}/lib" "${ULTRALIGHT_SDK_ROOT}/bin" NO_DEFAULT_PATH)
	find_library(ULTRALIGHT_LIB NAMES Ultralight PATHS "${ULTRALIGHT_SDK_ROOT}/lib" "${ULTRALIGHT_SDK_ROOT}/bin" NO_DEFAULT_PATH)
	find_library(ULTRALIGHT_CORE_LIB NAMES UltralightCore PATHS "${ULTRALIGHT_SDK_ROOT}/lib" "${ULTRALIGHT_SDK_ROOT}/bin" NO_DEFAULT_PATH)
	find_library(WEB_CORE_LIB NAMES WebCore PATHS "${ULTRALIGHT_SDK_ROOT}/lib" "${ULTRALIGHT_SDK_ROOT}/bin" NO_DEFAULT_PATH)

	if(NOT APP_CORE_LIB OR NOT ULTRALIGHT_LIB OR NOT ULTRALIGHT_CORE_LIB OR NOT WEB_CORE_LIB)
		message(FATAL_ERROR "Failed to locate Ultralight libraries under ${ULTRALIGHT_SDK_ROOT}.")
	endif()

	target_link_libraries(${NAME} PRIVATE
		${APP_CORE_LIB}
		${ULTRALIGHT_LIB}
		${ULTRALIGHT_CORE_LIB}
		${WEB_CORE_LIB}
	)

	if(APPLE OR UNIX)
		set_target_properties(${NAME} PROPERTIES BUILD_RPATH "$ORIGIN" INSTALL_RPATH "$ORIGIN")
	endif()
endfunction()

# Sources
set(SOURCES
	"src/Browser.h"
	"src/Browser.cpp"
	"src/AdBlocker.h"
	"src/AdBlocker.cpp"
	"src/BookmarkStore.h"
	"src/BookmarkStore.cpp"
	"src/ThemeManager.h"
	"src/ThemeManager.cpp"
	"src/DownloadManager.h"
	"src/DownloadManager.cpp"
	"src/ExtensionManager.h"
	"src/ExtensionManager.cpp"
	"src/PasswordManager.h"
	"src/PasswordManager.cpp"
	"src/Tab.h"
	"src/Tab.cpp"
	"src/UI.h"
	"src/UI.cpp"
  "src/Utils.cpp"
  "src/Settings.cpp"
	"src/drm/DRMSettings.h"
	"src/drm/DRMSettings.cpp"
	"src/drm/DRMDependencyManager.h"
	"src/drm/DRMDependencyManager.cpp"
	"src/drm/DRMWebViewTab.h"
	"src/drm/DRMWebViewTab.cpp"
	"src/drm/DRMWebViewManager.h"
	"src/drm/DRMWebViewManager.cpp"
	"src/main.cpp"
	"${CMAKE_CURRENT_BINARY_DIR}/browser.rc"
)

if(WIN32)
	list(APPEND SOURCES
		"src/drm/DRMDependencyManager_windows.cpp"
		"src/drm/DRMWebViewTab_windows.cpp")
elseif(APPLE)
	list(APPEND SOURCES
		"src/drm/DRMDependencyManager_mac.cpp"
		"src/drm/DRMWebViewTab_mac.mm")
elseif(UNIX)
	list(APPEND SOURCES
		"src/drm/DRMDependencyManager_linux.cpp"
		"src/drm/DRMWebViewTab_linux.cpp")
endif()

add_app(Ultralight-WebBrowser ${SOURCES})

# On Linux, DRM webview support depends on GTK and WebKit2GTK.
# We require these packages in CI and link them when present.
if(UNIX AND NOT APPLE)
	find_package(PkgConfig REQUIRED)
	pkg_check_modules(GTK3 REQUIRED gtk+-3.0)
	# Prefer WebKit2GTK 4.1 (Ubuntu 24.04 default), but fall back to 4.0
	pkg_check_modules(WEBKIT2_41 QUIET webkit2gtk-4.1)
	if(NOT WEBKIT2_41_FOUND)
		pkg_check_modules(WEBKIT2 REQUIRED webkit2gtk-4.0)
	else()
		set(WEBKIT2_FOUND TRUE)
		set(WEBKIT2_INCLUDE_DIRS ${WEBKIT2_41_INCLUDE_DIRS})
		set(WEBKIT2_LIBRARIES ${WEBKIT2_41_LIBRARIES})
	endif()
	message(STATUS "Building DRM Linux WebView using GTK3/WebKit2GTK")
	target_include_directories(Ultralight-WebBrowser PRIVATE ${GTK3_INCLUDE_DIRS} ${WEBKIT2_INCLUDE_DIRS})
	target_link_libraries(Ultralight-WebBrowser PRIVATE ${GTK3_LIBRARIES} ${WEBKIT2_LIBRARIES})
endif()

# On macOS, DRM webview support requires Cocoa and WebKit frameworks.
if(APPLE)
	find_library(COCOA_FRAMEWORK Cocoa REQUIRED)
	find_library(WEBKIT_FRAMEWORK WebKit REQUIRED)
	find_library(FOUNDATION_FRAMEWORK Foundation REQUIRED)
	message(STATUS "Building DRM macOS WebView using Cocoa/WebKit frameworks")
	target_link_libraries(Ultralight-WebBrowser PRIVATE
		${COCOA_FRAMEWORK}
		${WEBKIT_FRAMEWORK}
		${FOUNDATION_FRAMEWORK}
		"-framework AppKit"
	)
endif()

# On Windows, DRM webview support uses Microsoft WebView2.
# Download and configure WebView2 SDK via FetchContent.
if(WIN32 AND MSVC)
	include(FetchContent)

	# Download WebView2 SDK from NuGet
	FetchContent_Declare(
		webview2
		URL https://www.nuget.org/api/v2/package/Microsoft.Web.WebView2/1.0.2739.15
		DOWNLOAD_EXTRACT_TIMESTAMP TRUE
	)
	FetchContent_MakeAvailable(webview2)

	# WebView2 headers and libs from NuGet package
	set(WEBVIEW2_INCLUDE_DIR "${webview2_SOURCE_DIR}/build/native/include")
	set(WEBVIEW2_LIB_DIR "${webview2_SOURCE_DIR}/build/native/x64")

	if(EXISTS "${WEBVIEW2_INCLUDE_DIR}/WebView2.h")
		message(STATUS "Building DRM Windows WebView using Microsoft WebView2 SDK")
		target_include_directories(Ultralight-WebBrowser PRIVATE ${WEBVIEW2_INCLUDE_DIR})
		target_link_libraries(Ultralight-WebBrowser PRIVATE "${WEBVIEW2_LIB_DIR}/WebView2LoaderStatic.lib")
	else()
		message(WARNING "WebView2 SDK download failed or not found. DRM WebView will use stub implementation.")
	endif()
endif()

 # Unit test for utils
 enable_testing()

 # Only build tests in environments where linking to the Ultralight
 # libraries is supported.
 if(NOT (WIN32 AND NOT MSVC))
	 include(FetchContent)
	 set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
	 FetchContent_Declare(
	 	 googletest
	 	 GIT_REPOSITORY https://github.com/google/googletest.git
	 	 GIT_TAG v1.14.0
	 )
	 FetchContent_MakeAvailable(googletest)

	 # UtilsTest requires Ultralight SDK libraries. Only build if SDK libs are found.
	 find_library(APP_CORE_LIB NAMES AppCore PATHS "${ULTRALIGHT_SDK_ROOT}/lib" "${ULTRALIGHT_SDK_ROOT}/bin" NO_DEFAULT_PATH)
	 find_library(ULTRALIGHT_LIB NAMES Ultralight PATHS "${ULTRALIGHT_SDK_ROOT}/lib" "${ULTRALIGHT_SDK_ROOT}/bin" NO_DEFAULT_PATH)
	 find_library(ULTRALIGHT_CORE_LIB NAMES UltralightCore PATHS "${ULTRALIGHT_SDK_ROOT}/lib" "${ULTRALIGHT_SDK_ROOT}/bin" NO_DEFAULT_PATH)
	 find_library(WEB_CORE_LIB NAMES WebCore PATHS "${ULTRALIGHT_SDK_ROOT}/lib" "${ULTRALIGHT_SDK_ROOT}/bin" NO_DEFAULT_PATH)

	 if(APP_CORE_LIB AND ULTRALIGHT_LIB)
		 add_executable(UtilsTest tests/UtilsTest.cpp src/Utils.cpp)
		 target_link_libraries(UtilsTest PRIVATE ${APP_CORE_LIB} ${ULTRALIGHT_LIB} ${ULTRALIGHT_CORE_LIB} ${WEB_CORE_LIB})
		 target_include_directories(UtilsTest PRIVATE "${ULTRALIGHT_SDK_ROOT}/include")
		 # Set RPATH to find SDK libraries at runtime (for Linux/macOS)
		 if(NOT WIN32)
			 set_target_properties(UtilsTest PROPERTIES
				 BUILD_RPATH "${ULTRALIGHT_SDK_ROOT}/lib"
				 INSTALL_RPATH "${ULTRALIGHT_SDK_ROOT}/lib"
			 )
		 endif()
		 add_test(NAME UtilsTest COMMAND UtilsTest)
		 message(STATUS "Building UtilsTest with Ultralight SDK libraries")
	 else()
		 message(STATUS "Skipping UtilsTest: Ultralight SDK libraries not found")
	 endif()

	 # DRMSettingsTest doesn't need Ultralight libraries - it's standalone
	 add_executable(DRMSettingsTest
	 	tests/DRMSettingsTest.cpp
	 	src/drm/DRMSettings.cpp)
	 target_link_libraries(DRMSettingsTest PRIVATE GTest::gtest_main)
	 add_test(NAME DRMSettingsTest COMMAND DRMSettingsTest)
 else()
	 message(STATUS "Skipping tests on Windows when not using MSVC (to avoid linking MSVC .lib with MinGW)")
 endif()

# Install/package settings
set(INSTALL_DIR_NAME "UltralightWebBrowser")
install(TARGETS Ultralight-WebBrowser RUNTIME DESTINATION ${INSTALL_DIR_NAME})
install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/assets" DESTINATION ${INSTALL_DIR_NAME})

# Windows: include SDK DLLs and ICU files if present
if(WIN32)
	file(GLOB ULTRALIGHT_DLLS RELATIVE "${ULTRALIGHT_SDK_ROOT}/bin" "${ULTRALIGHT_SDK_ROOT}/bin/*.dll")
	list(LENGTH ULTRALIGHT_DLLS _dll_count)
	if(_dll_count GREATER 0)
		# install expects full paths; prepend the bin folder
		set(_dll_fullpaths)
		foreach(_d ${ULTRALIGHT_DLLS})
			list(APPEND _dll_fullpaths "${ULTRALIGHT_SDK_ROOT}/bin/${_d}")
		endforeach()
		install(FILES ${_dll_fullpaths} DESTINATION ${INSTALL_DIR_NAME})
	endif()

	file(GLOB ICU_FILES "${ULTRALIGHT_SDK_ROOT}/bin/ICU/icudt*.dat" "${ULTRALIGHT_SDK_ROOT}/bin/ICU/cacert.pem")
	list(LENGTH ICU_FILES _icu_count)
	if(_icu_count GREATER 0)
		install(FILES ${ICU_FILES} DESTINATION ${INSTALL_DIR_NAME}/assets/resources)
	endif()
endif()

# Linux: include SDK shared libraries (.so) and resources
if(UNIX AND NOT APPLE)
	file(GLOB ULTRALIGHT_SOS "${ULTRALIGHT_SDK_ROOT}/lib/*.so*")
	list(LENGTH ULTRALIGHT_SOS _so_count)
	if(_so_count GREATER 0)
		install(FILES ${ULTRALIGHT_SOS} DESTINATION ${INSTALL_DIR_NAME})
	endif()

	# Also check for libraries in bin directory (some SDK layouts)
	file(GLOB ULTRALIGHT_BIN_SOS "${ULTRALIGHT_SDK_ROOT}/bin/*.so*")
	list(LENGTH ULTRALIGHT_BIN_SOS _bin_so_count)
	if(_bin_so_count GREATER 0)
		install(FILES ${ULTRALIGHT_BIN_SOS} DESTINATION ${INSTALL_DIR_NAME})
	endif()

	# ICU data files (check multiple possible locations)
	file(GLOB ICU_FILES_LIB "${ULTRALIGHT_SDK_ROOT}/lib/ICU/icudt*.dat" "${ULTRALIGHT_SDK_ROOT}/lib/ICU/cacert.pem")
	file(GLOB ICU_FILES_BIN "${ULTRALIGHT_SDK_ROOT}/bin/ICU/icudt*.dat" "${ULTRALIGHT_SDK_ROOT}/bin/ICU/cacert.pem")
	file(GLOB ICU_FILES_RES "${ULTRALIGHT_SDK_ROOT}/resources/icudt*.dat" "${ULTRALIGHT_SDK_ROOT}/resources/cacert.pem")
	set(ICU_FILES ${ICU_FILES_LIB} ${ICU_FILES_BIN} ${ICU_FILES_RES})
	list(LENGTH ICU_FILES _icu_count)
	if(_icu_count GREATER 0)
		install(FILES ${ICU_FILES} DESTINATION ${INSTALL_DIR_NAME}/assets/resources)
	endif()

	# Include resources directory if it exists
	if(EXISTS "${ULTRALIGHT_SDK_ROOT}/resources")
		install(DIRECTORY "${ULTRALIGHT_SDK_ROOT}/resources/" DESTINATION ${INSTALL_DIR_NAME}/assets/resources
			FILES_MATCHING PATTERN "*" PATTERN "*.dat" PATTERN "*.pem" PATTERN "*.bin")
	endif()
endif()

# macOS: include SDK dynamic libraries (.dylib) and resources
if(APPLE)
	file(GLOB ULTRALIGHT_DYLIBS "${ULTRALIGHT_SDK_ROOT}/lib/*.dylib*")
	list(LENGTH ULTRALIGHT_DYLIBS _dylib_count)
	if(_dylib_count GREATER 0)
		install(FILES ${ULTRALIGHT_DYLIBS} DESTINATION ${INSTALL_DIR_NAME})
	endif()

	# Also check for libraries in bin directory (some SDK layouts)
	file(GLOB ULTRALIGHT_BIN_DYLIBS "${ULTRALIGHT_SDK_ROOT}/bin/*.dylib*")
	list(LENGTH ULTRALIGHT_BIN_DYLIBS _bin_dylib_count)
	if(_bin_dylib_count GREATER 0)
		install(FILES ${ULTRALIGHT_BIN_DYLIBS} DESTINATION ${INSTALL_DIR_NAME})
	endif()

	# ICU data files (check multiple possible locations)
	file(GLOB ICU_FILES_LIB "${ULTRALIGHT_SDK_ROOT}/lib/ICU/icudt*.dat" "${ULTRALIGHT_SDK_ROOT}/lib/ICU/cacert.pem")
	file(GLOB ICU_FILES_BIN "${ULTRALIGHT_SDK_ROOT}/bin/ICU/icudt*.dat" "${ULTRALIGHT_SDK_ROOT}/bin/ICU/cacert.pem")
	file(GLOB ICU_FILES_RES "${ULTRALIGHT_SDK_ROOT}/resources/icudt*.dat" "${ULTRALIGHT_SDK_ROOT}/resources/cacert.pem")
	set(ICU_FILES ${ICU_FILES_LIB} ${ICU_FILES_BIN} ${ICU_FILES_RES})
	list(LENGTH ICU_FILES _icu_count)
	if(_icu_count GREATER 0)
		install(FILES ${ICU_FILES} DESTINATION ${INSTALL_DIR_NAME}/assets/resources)
	endif()

	# Include resources directory if it exists
	if(EXISTS "${ULTRALIGHT_SDK_ROOT}/resources")
		install(DIRECTORY "${ULTRALIGHT_SDK_ROOT}/resources/" DESTINATION ${INSTALL_DIR_NAME}/assets/resources
			FILES_MATCHING PATTERN "*" PATTERN "*.dat" PATTERN "*.pem" PATTERN "*.bin")
	endif()
endif()

# Post-build: copy assets and runtime files next to the exe for running from build dir
if(WIN32)
	add_custom_command(TARGET Ultralight-WebBrowser POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/assets" "$<TARGET_FILE_DIR:Ultralight-WebBrowser>/assets"
	)
	foreach(_dll IN ITEMS AppCore.dll Ultralight.dll UltralightCore.dll WebCore.dll)
		add_custom_command(TARGET Ultralight-WebBrowser POST_BUILD
			COMMAND ${CMAKE_COMMAND} -E copy_if_different "${ULTRALIGHT_SDK_ROOT}/bin/${_dll}" "$<TARGET_FILE_DIR:Ultralight-WebBrowser>/${_dll}"
		)
	endforeach()
	add_custom_command(TARGET Ultralight-WebBrowser POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:Ultralight-WebBrowser>/assets/resources"
	)
	add_custom_command(TARGET Ultralight-WebBrowser POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E copy_if_different "${ULTRALIGHT_SDK_ROOT}/bin/ICU/icudt67l.dat" "$<TARGET_FILE_DIR:Ultralight-WebBrowser>/assets/resources/icudt67l.dat"
	)
	add_custom_command(TARGET Ultralight-WebBrowser POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E copy_if_different "${ULTRALIGHT_SDK_ROOT}/bin/ICU/cacert.pem" "$<TARGET_FILE_DIR:Ultralight-WebBrowser>/assets/resources/cacert.pem"
	)
endif()

# Post-build: copy assets and runtime files for Linux
if(UNIX AND NOT APPLE)
	add_custom_command(TARGET Ultralight-WebBrowser POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/assets" "$<TARGET_FILE_DIR:Ultralight-WebBrowser>/assets"
	)
	# Copy shared libraries from lib directory
	file(GLOB _linux_sos "${ULTRALIGHT_SDK_ROOT}/lib/*.so*")
	foreach(_so ${_linux_sos})
		add_custom_command(TARGET Ultralight-WebBrowser POST_BUILD
			COMMAND ${CMAKE_COMMAND} -E copy_if_different "${_so}" "$<TARGET_FILE_DIR:Ultralight-WebBrowser>/"
		)
	endforeach()
	# Also check bin directory
	file(GLOB _linux_bin_sos "${ULTRALIGHT_SDK_ROOT}/bin/*.so*")
	foreach(_so ${_linux_bin_sos})
		add_custom_command(TARGET Ultralight-WebBrowser POST_BUILD
			COMMAND ${CMAKE_COMMAND} -E copy_if_different "${_so}" "$<TARGET_FILE_DIR:Ultralight-WebBrowser>/"
		)
	endforeach()
	add_custom_command(TARGET Ultralight-WebBrowser POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:Ultralight-WebBrowser>/assets/resources"
	)
	# Copy ICU files from various possible locations
	if(EXISTS "${ULTRALIGHT_SDK_ROOT}/bin/ICU/icudt67l.dat")
		add_custom_command(TARGET Ultralight-WebBrowser POST_BUILD
			COMMAND ${CMAKE_COMMAND} -E copy_if_different "${ULTRALIGHT_SDK_ROOT}/bin/ICU/icudt67l.dat" "$<TARGET_FILE_DIR:Ultralight-WebBrowser>/assets/resources/icudt67l.dat"
		)
	elseif(EXISTS "${ULTRALIGHT_SDK_ROOT}/lib/ICU/icudt67l.dat")
		add_custom_command(TARGET Ultralight-WebBrowser POST_BUILD
			COMMAND ${CMAKE_COMMAND} -E copy_if_different "${ULTRALIGHT_SDK_ROOT}/lib/ICU/icudt67l.dat" "$<TARGET_FILE_DIR:Ultralight-WebBrowser>/assets/resources/icudt67l.dat"
		)
	elseif(EXISTS "${ULTRALIGHT_SDK_ROOT}/resources/icudt67l.dat")
		add_custom_command(TARGET Ultralight-WebBrowser POST_BUILD
			COMMAND ${CMAKE_COMMAND} -E copy_if_different "${ULTRALIGHT_SDK_ROOT}/resources/icudt67l.dat" "$<TARGET_FILE_DIR:Ultralight-WebBrowser>/assets/resources/icudt67l.dat"
		)
	endif()
	if(EXISTS "${ULTRALIGHT_SDK_ROOT}/bin/ICU/cacert.pem")
		add_custom_command(TARGET Ultralight-WebBrowser POST_BUILD
			COMMAND ${CMAKE_COMMAND} -E copy_if_different "${ULTRALIGHT_SDK_ROOT}/bin/ICU/cacert.pem" "$<TARGET_FILE_DIR:Ultralight-WebBrowser>/assets/resources/cacert.pem"
		)
	elseif(EXISTS "${ULTRALIGHT_SDK_ROOT}/lib/ICU/cacert.pem")
		add_custom_command(TARGET Ultralight-WebBrowser POST_BUILD
			COMMAND ${CMAKE_COMMAND} -E copy_if_different "${ULTRALIGHT_SDK_ROOT}/lib/ICU/cacert.pem" "$<TARGET_FILE_DIR:Ultralight-WebBrowser>/assets/resources/cacert.pem"
		)
	elseif(EXISTS "${ULTRALIGHT_SDK_ROOT}/resources/cacert.pem")
		add_custom_command(TARGET Ultralight-WebBrowser POST_BUILD
			COMMAND ${CMAKE_COMMAND} -E copy_if_different "${ULTRALIGHT_SDK_ROOT}/resources/cacert.pem" "$<TARGET_FILE_DIR:Ultralight-WebBrowser>/assets/resources/cacert.pem"
		)
	endif()
endif()

# Post-build: copy assets and runtime files for macOS
if(APPLE)
	add_custom_command(TARGET Ultralight-WebBrowser POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/assets" "$<TARGET_FILE_DIR:Ultralight-WebBrowser>/assets"
	)
	# Copy dynamic libraries from lib directory
	file(GLOB _macos_dylibs "${ULTRALIGHT_SDK_ROOT}/lib/*.dylib*")
	foreach(_dylib ${_macos_dylibs})
		add_custom_command(TARGET Ultralight-WebBrowser POST_BUILD
			COMMAND ${CMAKE_COMMAND} -E copy_if_different "${_dylib}" "$<TARGET_FILE_DIR:Ultralight-WebBrowser>/"
		)
	endforeach()
	# Also check bin directory
	file(GLOB _macos_bin_dylibs "${ULTRALIGHT_SDK_ROOT}/bin/*.dylib*")
	foreach(_dylib ${_macos_bin_dylibs})
		add_custom_command(TARGET Ultralight-WebBrowser POST_BUILD
			COMMAND ${CMAKE_COMMAND} -E copy_if_different "${_dylib}" "$<TARGET_FILE_DIR:Ultralight-WebBrowser>/"
		)
	endforeach()
	add_custom_command(TARGET Ultralight-WebBrowser POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:Ultralight-WebBrowser>/assets/resources"
	)
	# Copy ICU files from various possible locations
	if(EXISTS "${ULTRALIGHT_SDK_ROOT}/bin/ICU/icudt67l.dat")
		add_custom_command(TARGET Ultralight-WebBrowser POST_BUILD
			COMMAND ${CMAKE_COMMAND} -E copy_if_different "${ULTRALIGHT_SDK_ROOT}/bin/ICU/icudt67l.dat" "$<TARGET_FILE_DIR:Ultralight-WebBrowser>/assets/resources/icudt67l.dat"
		)
	elseif(EXISTS "${ULTRALIGHT_SDK_ROOT}/lib/ICU/icudt67l.dat")
		add_custom_command(TARGET Ultralight-WebBrowser POST_BUILD
			COMMAND ${CMAKE_COMMAND} -E copy_if_different "${ULTRALIGHT_SDK_ROOT}/lib/ICU/icudt67l.dat" "$<TARGET_FILE_DIR:Ultralight-WebBrowser>/assets/resources/icudt67l.dat"
		)
	elseif(EXISTS "${ULTRALIGHT_SDK_ROOT}/resources/icudt67l.dat")
		add_custom_command(TARGET Ultralight-WebBrowser POST_BUILD
			COMMAND ${CMAKE_COMMAND} -E copy_if_different "${ULTRALIGHT_SDK_ROOT}/resources/icudt67l.dat" "$<TARGET_FILE_DIR:Ultralight-WebBrowser>/assets/resources/icudt67l.dat"
		)
	endif()
	if(EXISTS "${ULTRALIGHT_SDK_ROOT}/bin/ICU/cacert.pem")
		add_custom_command(TARGET Ultralight-WebBrowser POST_BUILD
			COMMAND ${CMAKE_COMMAND} -E copy_if_different "${ULTRALIGHT_SDK_ROOT}/bin/ICU/cacert.pem" "$<TARGET_FILE_DIR:Ultralight-WebBrowser>/assets/resources/cacert.pem"
		)
	elseif(EXISTS "${ULTRALIGHT_SDK_ROOT}/lib/ICU/cacert.pem")
		add_custom_command(TARGET Ultralight-WebBrowser POST_BUILD
			COMMAND ${CMAKE_COMMAND} -E copy_if_different "${ULTRALIGHT_SDK_ROOT}/lib/ICU/cacert.pem" "$<TARGET_FILE_DIR:Ultralight-WebBrowser>/assets/resources/cacert.pem"
		)
	elseif(EXISTS "${ULTRALIGHT_SDK_ROOT}/resources/cacert.pem")
		add_custom_command(TARGET Ultralight-WebBrowser POST_BUILD
			COMMAND ${CMAKE_COMMAND} -E copy_if_different "${ULTRALIGHT_SDK_ROOT}/resources/cacert.pem" "$<TARGET_FILE_DIR:Ultralight-WebBrowser>/assets/resources/cacert.pem"
		)
	endif()
endif()

# CPack (global scope)
set(WEBBROWSER_VERSION "dev" CACHE STRING "WebBrowser version to use in package names (defaults to 'dev')")
set(CPACK_PACKAGE_NAME "Ultralight-WebBrowser")
set(CPACK_PACKAGE_VENDOR "Ultralight")
set(CPACK_PACKAGE_VERSION "${WEBBROWSER_VERSION}")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
set(CPACK_OUTPUT_FILE_PREFIX "${CMAKE_BINARY_DIR}")
set(CPACK_STRIP_FILES ON)

# Ensure a maintainer/contact is set for Debian packages (required by CPackDeb)
# CI environments may not set these, so provide a sensible default that can
# be overridden by callers (e.g. -DCPACK_PACKAGE_CONTACT=you@example.com).
# Updated for CI fixes.
if(NOT DEFINED CPACK_PACKAGE_CONTACT)
	set(CPACK_PACKAGE_CONTACT "maintainer@example.com" CACHE STRING "Contact for binary packages (email)")
endif()
if(NOT DEFINED CPACK_DEBIAN_PACKAGE_MAINTAINER)
	set(CPACK_DEBIAN_PACKAGE_MAINTAINER "${CPACK_PACKAGE_CONTACT}" CACHE STRING "Debian package maintainer")
endif()

set(APP_ICON_ICO "")
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/browser.ico")
	set(APP_ICON_ICO "${CMAKE_CURRENT_SOURCE_DIR}/browser.ico")
elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/assets/icon.ico")
	set(APP_ICON_ICO "${CMAKE_CURRENT_SOURCE_DIR}/assets/icon.ico")
endif()

configure_file("${CMAKE_CURRENT_SOURCE_DIR}/browser.rc" "${CMAKE_CURRENT_BINARY_DIR}/browser.rc" @ONLY)

if(WIN32)
	if(NOT DEFINED CPACK_GENERATOR)
		if(CREATE_INSTALLER)
			set(CPACK_GENERATOR "NSIS;ZIP")
		else()
			set(CPACK_GENERATOR "ZIP")
		endif()
	endif()
	set(CPACK_PACKAGE_INSTALL_DIRECTORY "Ultralight Web Browser")
	set(CPACK_NSIS_DISPLAY_NAME "Ultralight Web Browser")
	set(CPACK_NSIS_MUI_FINISHPAGE_RUN "${INSTALL_DIR_NAME}\\Ultralight-WebBrowser.exe")
	if(APP_ICON_ICO)
		set(CPACK_NSIS_MUI_ICON "${APP_ICON_ICO}")
		set(CPACK_NSIS_MUI_UNIICON "${APP_ICON_ICO}")
	endif()

	# Configure Start Menu shortcuts to point to the correct executable
	set(CPACK_NSIS_INSTALLED_ICON_NAME "${INSTALL_DIR_NAME}\\\\Ultralight-WebBrowser.exe")
	set(CPACK_NSIS_CREATE_ICONS_EXTRA "
		CreateShortCut \\\"$SMPROGRAMS\\\\$STARTMENU_FOLDER\\\\Ultralight Web Browser.lnk\\\" \\\"$INSTDIR\\\\${INSTALL_DIR_NAME}\\\\Ultralight-WebBrowser.exe\\\" \\\"\\\" \\\"$INSTDIR\\\\${INSTALL_DIR_NAME}\\\\Ultralight-WebBrowser.exe\\\" 0
	")
	set(CPACK_NSIS_DELETE_ICONS_EXTRA "
		Delete \\\"$SMPROGRAMS\\\\$STARTMENU_FOLDER\\\\Ultralight Web Browser.lnk\\\"
	")
	set(CPACK_NSIS_MENU_LINKS
		"${INSTALL_DIR_NAME}/Ultralight-WebBrowser.exe" "Ultralight Web Browser"
	)
else()
	if(NOT DEFINED CPACK_GENERATOR)
		set(CPACK_GENERATOR "TGZ")
	endif()
endif()

if(CMAKE_SIZEOF_VOID_P EQUAL 8)
	set(ARCH_SUFFIX "x64")
elseif(CMAKE_SIZEOF_VOID_P EQUAL 4)
	set(ARCH_SUFFIX "x86")
else()
	set(ARCH_SUFFIX ${CMAKE_SYSTEM_PROCESSOR})
endif()

if(APPLE)
	set(OS_NAME "macOS")
elseif(WIN32)
	set(OS_NAME "Windows")
elseif(UNIX)
	set(OS_NAME "Linux")
else()
	set(OS_NAME ${CMAKE_SYSTEM_NAME})
endif()

set(CPACK_PACKAGE_FILE_NAME "${CPACK_PACKAGE_NAME}-${CPACK_PACKAGE_VERSION}-${OS_NAME}-${ARCH_SUFFIX}")
include(CPack)
