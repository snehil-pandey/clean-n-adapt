cmake_minimum_required(VERSION 3.16)

# Single source of truth for the version: the VERSION file (three numbers, e.g. 0.1.0).
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" CNA_VERSION_RAW)
string(STRIP "${CNA_VERSION_RAW}" CNA_VERSION)
project(cna VERSION ${CNA_VERSION} LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()
if(MSVC)
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded")   # static CRT: single exe, no redistributable needed
endif()
if(WIN32)
  enable_language(RC)
endif()

# 0.1.0 -> 0,1,0,0 for the Windows VERSIONINFO resource
string(REPLACE "." "," CNA_VER_COMMA "${CNA_VERSION}.0")
configure_file(assets/version_info.h.in "${CMAKE_BINARY_DIR}/version_info.h" @ONLY)

# ---------------------------------------------------------------------------------------------
# cna  - the command-line engine (builds on Windows; also on Linux for development/testing)
# ---------------------------------------------------------------------------------------------
file(GLOB CNA_SRC CONFIGURE_DEPENDS src/*.cpp)
if(WIN32)
  list(APPEND CNA_SRC assets/cna.rc)
endif()
add_executable(cna ${CNA_SRC})
target_compile_definitions(cna PRIVATE CNA_VERSION="${CNA_VERSION}")
if(MSVC)
  target_compile_options(cna PRIVATE /O1 /GL /utf-8 /EHsc /W3)
  target_link_options(cna PRIVATE /LTCG /OPT:REF /OPT:ICF)
else()
  target_compile_options(cna PRIVATE -Os -ffunction-sections -fdata-sections -Wall -Wextra)
  target_link_options(cna PRIVATE -Wl,--gc-sections -s)
endif()

if(WIN32)
  target_include_directories(cna PRIVATE "${CMAKE_BINARY_DIR}" "${CMAKE_SOURCE_DIR}/assets") # for the .rc
  target_link_libraries(cna PRIVATE psapi advapi32 shell32)
  if(NOT MSVC)
    target_link_options(cna PRIVATE -static)
  endif()

  # -------------------------------------------------------------------------------------------
  # cna-widget - tray + desktop widget; drives cna.exe (must be installed next to it)
  # -------------------------------------------------------------------------------------------
  add_executable(cna-widget WIN32 widget/widget.cpp assets/widget.rc)
  target_include_directories(cna-widget PRIVATE "${CMAKE_BINARY_DIR}" "${CMAKE_SOURCE_DIR}/assets")
  target_link_libraries(cna-widget PRIVATE gdiplus dwmapi shell32 user32 gdi32 advapi32 ole32 uuid)
  if(MSVC)
    target_compile_definitions(cna-widget PRIVATE UNICODE _UNICODE)
    target_compile_options(cna-widget PRIVATE /O1 /GL /utf-8 /EHsc /W3)
    target_link_options(cna-widget PRIVATE /LTCG /OPT:REF /OPT:ICF /ENTRY:wWinMainCRTStartup)
  else()
    target_compile_options(cna-widget PRIVATE -Os -Wall -Wextra)
    target_link_options(cna-widget PRIVATE -municode -mwindows -static -s)
  endif()

  # Copy the finished binaries to <source>/dist next to the installer.
  foreach(t cna cna-widget)
    add_custom_command(TARGET ${t} POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_SOURCE_DIR}/dist"
      COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:${t}>" "${CMAKE_SOURCE_DIR}/dist/")
  endforeach()

  # -------------------------------------------------------------------------------------------
  # installer - Clean-n-Adapt-Setup-<version>.exe (NSIS). Built automatically when NSIS is found.
  # -------------------------------------------------------------------------------------------
  # Stage the executables first. Passing generator-produced build paths directly to NSIS was
  # unreliable in multi-config CI builds; this target gives the packager stable, real files.
  set(CNA_STAGE_DIR "${CMAKE_BINARY_DIR}/package/$<CONFIG>")
  add_custom_target(stage_release
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CNA_STAGE_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:cna>" "${CNA_STAGE_DIR}/cna.exe"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:cna-widget>" "${CNA_STAGE_DIR}/cna-widget.exe"
    DEPENDS cna cna-widget
    COMMENT "Staging release executables for the installer"
    VERBATIM)

  find_program(MAKENSIS_EXE makensis
    HINTS "$ENV{ProgramFiles}/NSIS" "$ENV{ProgramFiles\(x86\)}/NSIS" "C:/Program Files (x86)/NSIS" "C:/Program Files/NSIS")
  if(MAKENSIS_EXE)
    add_custom_target(installer ALL
      COMMAND "${MAKENSIS_EXE}" -V2
              "-DVERSION=${CNA_VERSION}"
              "-DCNA_EXE=${CNA_STAGE_DIR}/cna.exe"
              "-DWIDGET_EXE=${CNA_STAGE_DIR}/cna-widget.exe"
              "-DROOT=${CMAKE_SOURCE_DIR}"
              "-DOUT_DIR=${CMAKE_SOURCE_DIR}/dist"
              "${CMAKE_SOURCE_DIR}/installer/cna.nsi"
      DEPENDS stage_release
      COMMENT "Building installer dist/Clean-n-Adapt-Setup-${CNA_VERSION}.exe"
      VERBATIM)
  else()
    message(WARNING "NSIS (makensis) not found: the installer will NOT be built. "
                    "Install NSIS (Windows: winget install NSIS.NSIS, Debian/Ubuntu: apt install nsis) and re-run CMake.")
  endif()
endif()
