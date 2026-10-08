cmake_minimum_required(VERSION 3.16)
project(EarthView VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

option(EARTHVIEW_DEPLOY_QT "Run windeployqt after building EarthView" ON)

set(EARTHVIEW_QT_PREFIX "" CACHE PATH "Qt installation prefix")
if(EARTHVIEW_QT_PREFIX)
    list(PREPEND CMAKE_PREFIX_PATH "${EARTHVIEW_QT_PREFIX}")
elseif(EXISTS "C:/Qt/6.11.0/llvm-mingw_64")
    list(PREPEND CMAKE_PREFIX_PATH "C:/Qt/6.11.0/llvm-mingw_64")
elseif(EXISTS "C:/Qtfull/6.11.0/llvm-mingw_64")
    list(PREPEND CMAKE_PREFIX_PATH "C:/Qtfull/6.11.0/llvm-mingw_64")
endif()

find_package(Qt6 REQUIRED COMPONENTS Core Widgets Network OpenGL OpenGLWidgets Sql)
find_package(Qt6 QUIET COMPONENTS Test)
find_package(Qt6ZlibPrivate QUIET)

set(SOURCES
    main.cpp
    BorderRenderer.cpp
    Camera.cpp
    CityLoader.cpp
    CityRenderer.cpp
    DemLoader.cpp
    FrameProfiler.cpp
    GridRenderer.cpp
    LineBatchRenderer.cpp
    MainWindow.cpp
    MapViewWindow.cpp
    MapWidget.cpp
    MbTilesReader.cpp
    MercatorProjection.cpp
    OpenGLRuntime.cpp
    ShaderUtils.cpp
    StreamingBuffer.cpp
    TerrainRenderer.cpp
    TextRenderer.cpp
    TextureManager.cpp
    TileRenderer.cpp
    TMSLoader.cpp
    VectorTileRenderer.cpp
)

set(HEADERS
    BorderRenderer.h
    Camera.h
    CityLoader.h
    CityRenderer.h
    DemLoader.h
    GridRenderer.h
    LineBatchRenderer.h
    MainWindow.h
    MapViewWindow.h
    MapWidget.h
    MbTilesReader.h
    Constants.h
    FrameProfiler.h
    MercatorProjection.h
    OpenGLRuntime.h
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
    shaders/colored_terrain.vert
    shaders/instanced_rect_mercator.vert
    shaders/colored_line.frag
    shaders/solid_2d.vert
    shaders/solid_2d.frag
    shaders/mercator_line.vert
    shaders/terrain_line.vert
    shaders/point_marker.vert
    shaders/point_marker.frag
    shaders/terrain_height.vert
    shaders/terrain_height.frag
)

add_executable(${PROJECT_NAME}
    ${SOURCES}
    ${HEADERS}
    ${SHADERS}
)

target_link_libraries(${PROJECT_NAME}
    PRIVATE
        Qt6::Core
        Qt6::Widgets
        Qt6::Network
        Qt6::OpenGL
        Qt6::OpenGLWidgets
        Qt6::Sql
)

if(TARGET Qt6::ZlibPrivate)
    target_link_libraries(${PROJECT_NAME} PRIVATE Qt6::ZlibPrivate)
else()
    # Distro Qt builds use the system zlib and do not export Qt6::ZlibPrivate.
    find_package(ZLIB REQUIRED)
    target_link_libraries(${PROJECT_NAME} PRIVATE ZLIB::ZLIB)
endif()

add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/shaders"
        "$<TARGET_FILE_DIR:${PROJECT_NAME}>/shaders"
)

if(WIN32)
    target_link_libraries(${PROJECT_NAME} PRIVATE opengl32)
    if(EARTHVIEW_DEPLOY_QT)
        set(QT_BIN_DIR "")
        if(TARGET Qt6::qmake)
            get_target_property(QT_QMAKE_EXECUTABLE Qt6::qmake IMPORTED_LOCATION)
            if(QT_QMAKE_EXECUTABLE)
                get_filename_component(QT_BIN_DIR "${QT_QMAKE_EXECUTABLE}" DIRECTORY)
            endif()
        endif()

        find_program(WINDEPLOYQT_EXECUTABLE windeployqt HINTS "${QT_BIN_DIR}")
        if(WINDEPLOYQT_EXECUTABLE)
            add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E env
                    "PATH=${QT_BIN_DIR};$ENV{PATH}"
                    "${WINDEPLOYQT_EXECUTABLE}"
                    $<TARGET_FILE_DIR:${PROJECT_NAME}>
            )
        endif()
    endif()
endif()

# Offscreen snapshot tool (roadmap M0.3): same sources as the app, without main/MainWindow.
option(EARTHVIEW_BUILD_SNAPSHOT "Build the earthview_snapshot offscreen render tool" ON)
if(EARTHVIEW_BUILD_SNAPSHOT)
    set(SNAPSHOT_SOURCES ${SOURCES})
    list(REMOVE_ITEM SNAPSHOT_SOURCES main.cpp MainWindow.cpp MapViewWindow.cpp)
    set(SNAPSHOT_HEADERS ${HEADERS})
    list(REMOVE_ITEM SNAPSHOT_HEADERS MainWindow.h MapViewWindow.h)
    add_executable(earthview_snapshot tools/earthview_snapshot.cpp ${SNAPSHOT_SOURCES} ${SNAPSHOT_HEADERS})
    get_target_property(EARTHVIEW_LINK_LIBS ${PROJECT_NAME} LINK_LIBRARIES)
    target_link_libraries(earthview_snapshot PRIVATE ${EARTHVIEW_LINK_LIBS})
    add_custom_command(TARGET earthview_snapshot POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/shaders"
            "$<TARGET_FILE_DIR:earthview_snapshot>/shaders"
    )
endif()

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

option(EARTHVIEW_BUILD_TESTS "Build EarthView unit tests" ON)
if(EARTHVIEW_BUILD_TESTS AND TARGET Qt6::Test)
    enable_testing()
    add_subdirectory(tests)
endif()
