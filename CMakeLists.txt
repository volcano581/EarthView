cmake_minimum_required(VERSION 3.16)
project(EarthView VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
if(MSVC)
    # Qt 6.11's moc output (qtmochelpers.h) hits an MSVC generic-lambda bug in C++17 mode;
    # C++20 builds cleanly and matches the host project.
    set(CMAKE_CXX_STANDARD 20)
endif()
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

option(EARTHVIEW_BUILD_APP      "Build the EarthView standalone viewer app" ON)
option(EARTHVIEW_DEPLOY_QT      "Run windeployqt after building EarthView" ON)
option(EARTHVIEW_BUILD_SNAPSHOT "Build the earthview_snapshot offscreen render tool" ON)
option(EARTHVIEW_BUILD_TESTS    "Build EarthView unit tests" ON)
option(EARTHVIEW_BUILD_MAP_CONTROLS "Build the QML on-map controls (needs Qt Quick, Quick Widgets, Svg)" ON)

# Qt kit: EARTHVIEW_QT_PREFIX wins; CONQUER_QT_PREFIX (set by the Conquer superbuild) is
# checked next; otherwise a default kit matching the compiler is used as a fallback only,
# so an explicit -DCMAKE_PREFIX_PATH (e.g. the msvc2022_64 kit) is never overridden.
# The llvm-mingw kits only suit MinGW; MSVC needs msvc2022_64.
set(EARTHVIEW_QT_PREFIX "" CACHE PATH "Qt installation prefix")
if(EARTHVIEW_QT_PREFIX)
    list(PREPEND CMAKE_PREFIX_PATH "${EARTHVIEW_QT_PREFIX}")
elseif(DEFINED CONQUER_QT_PREFIX AND CONQUER_QT_PREFIX)
    list(PREPEND CMAKE_PREFIX_PATH "${CONQUER_QT_PREFIX}")
elseif(MINGW AND NOT CMAKE_PREFIX_PATH)
    foreach(_qt_kit "C:/Qt/6.11.0/llvm-mingw_64" "C:/Qtfull/6.11.0/llvm-mingw_64")
        if(EXISTS "${_qt_kit}")
            list(PREPEND CMAKE_PREFIX_PATH "${_qt_kit}")
            break()
        endif()
    endforeach()
elseif(MSVC AND EXISTS "C:/Qt/6.11.1/msvc2022_64")
    list(APPEND CMAKE_PREFIX_PATH "C:/Qt/6.11.1/msvc2022_64")
endif()

find_package(Qt6 REQUIRED COMPONENTS Core Widgets Network OpenGL OpenGLWidgets Sql)
find_package(Qt6 QUIET COMPONENTS Test)
if(EARTHVIEW_BUILD_MAP_CONTROLS)
    find_package(Qt6 QUIET COMPONENTS Qml Quick QuickWidgets Svg)
    if(NOT (TARGET Qt6::Quick AND TARGET Qt6::QuickWidgets AND TARGET Qt6::Svg))
        message(STATUS "EarthView: Qt Quick / Quick Widgets / Svg not found - map controls disabled")
        set(EARTHVIEW_BUILD_MAP_CONTROLS OFF)
    endif()
endif()
find_package(Qt6ZlibPrivate QUIET)

# ── Source lists ─────────────────────────────────────────────────────────────
# LIBRARY_SOURCES: the rendering core consumed by earthview_core, earthview_snapshot,
# and doctrine.  App-level files (main, MainWindow, MapViewWindow) live in the exe only.
set(LIBRARY_SOURCES
    BorderRenderer.cpp
    Camera.cpp
    CityLoader.cpp
    CityRenderer.cpp
    DemLoader.cpp
    FrameProfiler.cpp
    GridRenderer.cpp
    LineBatchRenderer.cpp
    MapSourceCatalog.cpp
    MapTheme.cpp
    MapWidget.cpp
    MbTilesReader.cpp
    MercatorProjection.cpp
    OpenGLRuntime.cpp
    RemoteMbTiles.cpp
    ShaderUtils.cpp
    StreamingBuffer.cpp
    TerrainRenderer.cpp
    TextRenderer.cpp
    TextureManager.cpp
    TileRenderer.cpp
    TMSLoader.cpp
    VectorTileRenderer.cpp
)

set(LIBRARY_HEADERS
    BorderRenderer.h
    Camera.h
    CityLoader.h
    CityRenderer.h
    Constants.h
    DemLoader.h
    FrameProfiler.h
    GridRenderer.h
    LineBatchRenderer.h
    MapFeatures.h
    MapSourceCatalog.h
    MapTheme.h
    MapWidget.h
    MbTilesReader.h
    MercatorProjection.h
    OpenGLRuntime.h
    RemoteMbTiles.h
    ShaderUtils.h
    StreamingBuffer.h
    TerrainRenderer.h
    TextRenderer.h
    TextureManager.h
    TileRenderer.h
    TMSLoader.h
    VectorTileRenderer.h
    fpsCounter.h
)

set(SHADERS
    shaders/textured_quad.vert
    shaders/textured_quad.frag
    shaders/colored_2d.vert
    shaders/colored_mercator.vert
    shaders/instanced_rect_mercator.vert
    shaders/colored_line.frag
    shaders/solid_2d.vert
    shaders/solid_2d.frag
    shaders/mercator_line.vert
    shaders/point_marker.vert
    shaders/point_marker.frag
    shaders/terrain_flat.vert
    shaders/terrain_height.frag
    shaders/vector_geometry.vert
    shaders/vector_line.vert
    shaders/vector_line.frag
    shaders/globe_composite.vert
    shaders/globe_composite.frag
    shaders/textured_globe.vert
    shaders/textured_globe.frag
)

# ── earthview_core: static library for doctrine and other hosts ───────────────
add_library(earthview_core STATIC ${LIBRARY_SOURCES} ${LIBRARY_HEADERS})
add_library(earthview::core ALIAS earthview_core)

target_include_directories(earthview_core
    PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
)

target_link_libraries(earthview_core
    PUBLIC
        Qt6::Core
        Qt6::Widgets
        Qt6::Network
        Qt6::OpenGL
        Qt6::OpenGLWidgets
        Qt6::Sql
)

if(TARGET Qt6::ZlibPrivate)
    target_link_libraries(earthview_core PRIVATE Qt6::ZlibPrivate)
else()
    # Distro Qt builds use the system zlib and do not export Qt6::ZlibPrivate.
    find_package(ZLIB REQUIRED)
    target_link_libraries(earthview_core PRIVATE ZLIB::ZLIB)
endif()

if(WIN32)
    target_link_libraries(earthview_core PUBLIC opengl32)
endif()

# Export paths for the Conquer superbuild (doctrine POST_BUILD + compile def).
set(EARTHVIEW_SHADER_DIR "${CMAKE_CURRENT_SOURCE_DIR}/shaders" CACHE INTERNAL "EarthView shaders directory")
set(EARTHVIEW_DATA_DIR   "${CMAKE_CURRENT_SOURCE_DIR}/Data"    CACHE INTERNAL "EarthView data directory")

if(EARTHVIEW_BUILD_MAP_CONTROLS)
    add_subdirectory(mapcontrols)
endif()

# ── EarthView: standalone viewer app ─────────────────────────────────────────
if(EARTHVIEW_BUILD_APP)
    add_executable(EarthView
        main.cpp
        MainWindow.cpp
        MainWindow.h
        MapViewWindow.cpp
        MapViewWindow.h
        ${SHADERS}
    )
    target_link_libraries(EarthView PRIVATE earthview::core)
    if(EARTHVIEW_BUILD_MAP_CONTROLS)
        target_link_libraries(EarthView PRIVATE earthview::mapcontrols)
        target_compile_definitions(EarthView PRIVATE EARTHVIEW_HAS_MAP_CONTROLS)
    endif()

    add_custom_command(TARGET EarthView POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/shaders"
            "$<TARGET_FILE_DIR:EarthView>/shaders"
    )

    if(WIN32 AND EARTHVIEW_DEPLOY_QT)
        set(QT_BIN_DIR "")
        if(TARGET Qt6::qmake)
            get_target_property(QT_QMAKE_EXECUTABLE Qt6::qmake IMPORTED_LOCATION)
            if(QT_QMAKE_EXECUTABLE)
                get_filename_component(QT_BIN_DIR "${QT_QMAKE_EXECUTABLE}" DIRECTORY)
            endif()
        endif()

        find_program(WINDEPLOYQT_EXECUTABLE windeployqt HINTS "${QT_BIN_DIR}")
        if(WINDEPLOYQT_EXECUTABLE)
            set(_earthview_qmldir_args "")
            if(EARTHVIEW_BUILD_MAP_CONTROLS)
                set(_earthview_qmldir_args --qmldir "${EARTHVIEW_MAPCONTROLS_QML_DIR}")
            endif()
            add_custom_command(TARGET EarthView POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E env
                    "PATH=${QT_BIN_DIR};$ENV{PATH}"
                    "${WINDEPLOYQT_EXECUTABLE}"
                    ${_earthview_qmldir_args}
                    $<TARGET_FILE:EarthView>
            )
        endif()
    endif()
endif()

# ── earthview_snapshot: offscreen render tool (roadmap M0.3) ─────────────────
if(EARTHVIEW_BUILD_SNAPSHOT)
    add_executable(earthview_snapshot tools/earthview_snapshot.cpp)
    target_link_libraries(earthview_snapshot PRIVATE earthview::core)
    add_custom_command(TARGET earthview_snapshot POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/shaders"
            "$<TARGET_FILE_DIR:earthview_snapshot>/shaders"
    )
endif()

# ── earthview_scene3d: double-precision WGS84/ECEF geodesy ───────────────────
# 3D globe math (roadmap M1+). Double-precision WGS84/ECEF via vendored GLM; no Qt dependency.
add_library(earthview_scene3d STATIC
    scene3d/Geodesy.cpp
    scene3d/Geodesy.h
    scene3d/Camera3D.cpp
    scene3d/Camera3D.h
)
target_include_directories(earthview_scene3d
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/scene3d"
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/glm"
)
set_target_properties(earthview_scene3d PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)

# osgEarth 3D view library (roadmap E1). The GL-free core always builds; the osgEarth
# widget only when osgEarth is found (EARTHVIEW_BUILD_3D), so 2D-only builds are unaffected.
add_subdirectory(earthview3d)
# Demo host (roadmap E2): feed library always; the app only when earthview3d is built.
add_subdirectory(earthview3d/demo)

# "Open 3D View" in the 2D viewer (roadmap E9). Only when the osgEarth widget was built: a
# 2D-only build has no action at all rather than one that cannot work. The deploy helper puts
# the OSG plugins and GDAL/PROJ data next to EarthView.exe, which the view needs at runtime.
if(TARGET earthview3d)
    target_link_libraries(${PROJECT_NAME} PRIVATE earthview3d)
    target_compile_definitions(${PROJECT_NAME} PRIVATE EARTHVIEW_HAVE_3D=1)
    earthview3d_deploy_runtime(${PROJECT_NAME})
endif()

option(EARTHVIEW_BUILD_TESTS "Build EarthView unit tests" ON)
if(EARTHVIEW_BUILD_TESTS AND TARGET Qt6::Test)
    enable_testing()
    add_subdirectory(tests)
endif()
