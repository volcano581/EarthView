cmake_minimum_required(VERSION 3.16)
project(EarthView VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

set(CMAKE_PREFIX_PATH "C:/Qtfull/6.11.0/llvm-mingw_64")
find_package(Qt6 REQUIRED COMPONENTS Core Widgets Network OpenGLWidgets)

set(SOURCES
    main.cpp
    BorderRenderer.cpp
    Camera.cpp
    CityLoader.cpp
    CityRenderer.cpp
    GridRenderer.cpp
    MainWindow.cpp
    MapWidget.cpp
    MercatorProjection.cpp
    ShaderUtils.cpp
    TextureManager.cpp
    TileRenderer.cpp
    TMSLoader.cpp
)

set(HEADERS
    BorderRenderer.h
    Camera.h
    CityLoader.h
    CityRenderer.h
    GridRenderer.h
    MainWindow.h
    MapWidget.h
    Constants.h
    MercatorProjection.h
    ShaderUtils.h
    TextureManager.h
    TileRenderer.h
    TMSLoader.h
    fpsCounter.h
)

set(SHADERS
    shaders/textured_quad.vert
    shaders/textured_quad.frag
    shaders/solid_2d.vert
    shaders/solid_2d.frag
    shaders/point_marker.vert
    shaders/point_marker.frag
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
        Qt6::OpenGLWidgets
)

add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/shaders"
        "$<TARGET_FILE_DIR:${PROJECT_NAME}>/shaders"
)

if(WIN32)
    target_link_libraries(${PROJECT_NAME} PRIVATE opengl32)
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E env
            "PATH=C:/Qtfull/6.11.0/llvm-mingw_64/bin;$ENV{PATH}"
            "C:/Qtfull/6.11.0/llvm-mingw_64/bin/windeployqt.exe"
            $<TARGET_FILE_DIR:${PROJECT_NAME}>
    )
endif()
