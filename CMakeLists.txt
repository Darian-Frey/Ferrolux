# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Shane Hartley
#
# Ferrolux RS-1 — Phase 1 (transport core).
#
# Toolchain minimums come from BUILD.md, with one correction: that document
# names Qt 6.5, which is unattainable on the reference platform. See BUG-001.
# Qt 6.4 is what Phase 1 actually requires and what is enforced here.

cmake_minimum_required(VERSION 3.21)

project(ferrolux
    VERSION 0.1.0
    DESCRIPTION "A cassette futurism audio player for Linux"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Qt6 6.4 REQUIRED COMPONENTS Core Gui Qml Quick)
qt_standard_project_setup()

find_package(PkgConfig REQUIRED)
pkg_check_modules(GStreamer REQUIRED IMPORTED_TARGET gstreamer-1.0>=1.20)
pkg_check_modules(TagLib REQUIRED IMPORTED_TARGET taglib>=1.12)
pkg_check_modules(GStreamerApp REQUIRED IMPORTED_TARGET gstreamer-app-1.0>=1.20)

qt_add_executable(ferrolux
    src/main.cpp
    src/core/Engine.cpp
    src/core/Engine.h
    src/core/Equaliser.cpp
    src/core/Equaliser.h
    src/meters/MeterSource.cpp
    src/meters/MeterSource.h
    src/meters/MeterTexture.cpp
    src/meters/MeterTexture.h
    src/library/PlaylistEntry.h
    src/library/PlaylistModel.cpp
    src/library/PlaylistModel.h
    src/library/MetadataReader.cpp
    src/library/MetadataReader.h
    src/library/PlaylistIO.cpp
    src/library/PlaylistIO.h
    src/library/PlaylistFilter.cpp
    src/library/PlaylistFilter.h
)

# RESOURCE_PREFIX is pinned rather than left to default: Qt 6.4 places QML
# resources under /<URI>/ and Qt 6.5 changed the default to /qt/qml/<URI>/, so
# an unpinned prefix makes the load path depend on the Qt version in use.
qt_add_qml_module(ferrolux
    URI Ferrolux
    VERSION 1.0
    RESOURCE_PREFIX /qt/qml
    QML_FILES qml/Main.qml
)

target_include_directories(ferrolux PRIVATE src)

target_link_libraries(ferrolux PRIVATE
    Qt6::Quick
    PkgConfig::GStreamer
    PkgConfig::TagLib
)

enable_testing()

# Phase 1 acceptance harness. Drives core/Engine headlessly against the
# criteria in ROADMAP.md Phase 1; needs two audio files passed on the command
# line, so it is registered only when FERROLUX_TEST_FLAC and
# FERROLUX_TEST_MP3 are set at configure time.
qt_add_executable(acceptance_transport
    tests/acceptance_transport.cpp
    src/core/Engine.cpp
    src/core/Engine.h
    src/core/Equaliser.cpp
    src/core/Equaliser.h
    src/library/PlaylistModel.cpp
    src/library/PlaylistModel.h
    src/library/PlaylistIO.cpp
    src/library/PlaylistIO.h
    src/meters/MeterSource.cpp
    src/meters/MeterSource.h
)
target_include_directories(acceptance_transport PRIVATE src)
target_link_libraries(acceptance_transport PRIVATE Qt6::Core PkgConfig::GStreamer)

# Phase 2 playlist tests. Pure logic over URLs, so these are self-contained.
qt_add_executable(playlist_model_test
    tests/playlist_model_test.cpp
    src/library/PlaylistModel.cpp
    src/library/PlaylistModel.h
    src/library/PlaylistIO.cpp
    src/library/PlaylistIO.h
    src/library/PlaylistFilter.cpp
    src/library/PlaylistFilter.h
)
target_include_directories(playlist_model_test PRIVATE src)
target_link_libraries(playlist_model_test PRIVATE Qt6::Core)
add_test(NAME playlist COMMAND playlist_model_test)

qt_add_executable(metadata_reader_test
    tests/metadata_reader_test.cpp
    src/library/MetadataReader.cpp
    src/library/MetadataReader.h
)
target_include_directories(metadata_reader_test PRIVATE src)
target_link_libraries(metadata_reader_test PRIVATE Qt6::Core PkgConfig::TagLib)

qt_add_executable(equaliser_test
    tests/equaliser_test.cpp
    src/core/Equaliser.cpp
    src/core/Equaliser.h
)
target_include_directories(equaliser_test PRIVATE src)
target_link_libraries(equaliser_test PRIVATE Qt6::Core PkgConfig::GStreamer PkgConfig::GStreamerApp)
add_test(NAME equaliser COMMAND equaliser_test)

qt_add_executable(meters_test
    tests/meters_test.cpp
    src/meters/MeterSource.cpp
    src/meters/MeterSource.h
    src/meters/MeterTexture.cpp
    src/meters/MeterTexture.h
)
target_include_directories(meters_test PRIVATE src)
target_link_libraries(meters_test PRIVATE Qt6::Core Qt6::Quick)
add_test(NAME meters COMMAND meters_test)

if(FERROLUX_TEST_FLAC AND FERROLUX_TEST_MP3)
    add_test(NAME transport
        COMMAND acceptance_transport ${FERROLUX_TEST_FLAC} ${FERROLUX_TEST_MP3})
    add_test(NAME metadata
        COMMAND metadata_reader_test ${FERROLUX_TEST_FLAC} ${FERROLUX_TEST_MP3})
endif()

install(TARGETS ferrolux
    BUNDLE  DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
