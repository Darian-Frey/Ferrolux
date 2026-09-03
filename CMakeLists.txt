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

find_package(Qt6 6.4 REQUIRED COMPONENTS Core Gui Qml Quick OpenGL ShaderTools)
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
    src/meters/FrameTimer.cpp
    src/meters/FrameTimer.h
    src/ui/ThemeTokens.cpp
    src/ui/ThemeTokens.h
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
# Fragment shaders for the meter display modes, compiled to .qsb by qsb at
# build time. Requires qt6-shadertools-dev; see BUILD.md.
qt_add_shaders(ferrolux "ferrolux_meter_shaders"
    PREFIX "/qt/qml/Ferrolux"
    FILES
        qml/shaders/spectrum.frag
        qml/shaders/vu.frag
        qml/shaders/ladder.frag
        qml/shaders/flame.frag
)

# A QML file carrying `pragma Singleton` is only registered as one if the
# generated qmldir says so, and that comes from this source property rather than
# from the pragma. Without it the type still registers, `Tokens` still resolves
# to an object, and every property on it reads as undefined — which surfaces as
# colours failing to assign rather than as anything naming the singleton.
set_source_files_properties(qml/Tokens.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

qt_add_qml_module(ferrolux
    URI Ferrolux
    VERSION 1.0
    RESOURCE_PREFIX /qt/qml
    QML_FILES
        qml/Main.qml
        qml/MeterDisplay.qml
        qml/Tokens.qml
        qml/Readout.qml
        qml/DisplayPanel.qml
)

# The four bundled faces (D-012) and the token sets (SPEC.md §Design tokens),
# compiled in rather than installed alongside. A panel that renders correctly
# only where its fonts happen to be installed is not resolution-independent
# chrome, it is a skin with an external dependency — and the point of D-012 was
# to remove exactly that class of surprise.
#
# Licence texts travel with the faces. They are redistributed under the SIL Open
# Font License 1.1, which requires the notice to accompany the font, and a
# notice left out of the binary is a notice that did not accompany anything.
qt_add_resources(ferrolux "ferrolux_panel_assets"
    PREFIX "/"
    FILES
        resources/fonts/DSEG7Classic-Regular.ttf
        resources/fonts/DSEG14Classic-Regular.ttf
        resources/fonts/Handjet-Panel.ttf
        resources/fonts/IBMPlexSansCondensed-Regular.ttf
        resources/fonts/OFL-DSEG.txt
        resources/fonts/OFL-Handjet.txt
        resources/fonts/OFL-IBMPlexSansCondensed.txt
        resources/themes/ferric.json
)

target_include_directories(ferrolux PRIVATE src)

target_link_libraries(ferrolux PRIVATE
    Qt6::Quick
    PkgConfig::GStreamer
    PkgConfig::TagLib
)

enable_testing()

# AV-002's offscreen half. Renders qml/MeterDisplay.qml — the same file the
# application instantiates, not a copy — through QQuickRenderControl at an
# arbitrary size, with a fence after each frame so the interval is the true
# cost of one. This is what answers the 3840x2160 and 30% headroom clauses that
# a managed window on this display cannot.
#
# Not registered with ctest. It needs a GPU and a display connection, and a
# machine that has neither would report a failure that says nothing about the
# code. tools/measure-frames.sh runs it.
qt_add_executable(frame_bench
    tests/frame_bench.cpp
    src/meters/FrameTimer.cpp
    src/meters/FrameTimer.h
    src/meters/MeterSource.cpp
    src/meters/MeterSource.h
    src/meters/MeterTexture.cpp
    src/meters/MeterTexture.h
)
target_include_directories(frame_bench PRIVATE src)
target_link_libraries(frame_bench PRIVATE Qt6::Quick Qt6::OpenGL)

# The QML is loaded from the source tree rather than from a resource, so that
# editing a shader and re-running the benchmark needs no rebuild of anything
# but the shader itself.
target_compile_definitions(frame_bench PRIVATE
    FERROLUX_QML_DIR="${CMAKE_CURRENT_SOURCE_DIR}/qml")

# The same shaders under the same resource prefix, so the fragmentShader paths
# inside MeterDisplay.qml resolve in this target exactly as they do in the app.
qt_add_shaders(frame_bench "frame_bench_meter_shaders"
    PREFIX "/qt/qml/Ferrolux"
    FILES
        qml/shaders/spectrum.frag
        qml/shaders/vu.frag
        qml/shaders/ladder.frag
        qml/shaders/flame.frag
)

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

# Phase 5 token set. Asserts that resources/themes/ferric.json still says what
# SPEC.md §Design tokens says it says, that every token qml/Tokens.qml names is
# present, and that the four faces load and report the family names the set asks
# for. Each of those is a way for the panel to go quietly wrong: a token that
# resolves to nothing draws black, and a face that fails to load substitutes a
# system font mid-readout.
qt_add_executable(tokens_test
    tests/tokens_test.cpp
    src/ui/ThemeTokens.cpp
    src/ui/ThemeTokens.h
)
target_include_directories(tokens_test PRIVATE src)
target_link_libraries(tokens_test PRIVATE Qt6::Gui)
add_test(NAME tokens COMMAND tokens_test "${CMAKE_CURRENT_SOURCE_DIR}")

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
