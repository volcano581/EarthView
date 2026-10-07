cmake_minimum_required(VERSION 3.21)
project(OsgEarthSpike LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

# Qt must be the MSVC kit (osgEarth from vcpkg is built with MSVC).
if(NOT CMAKE_PREFIX_PATH AND EXISTS "C:/Qt/6.11.0/msvc2022_64")
    list(PREPEND CMAKE_PREFIX_PATH "C:/Qt/6.11.0/msvc2022_64")
endif()

find_package(Qt6 REQUIRED COMPONENTS Widgets OpenGLWidgets)
find_package(OpenSceneGraph REQUIRED COMPONENTS osgDB osgGA osgUtil osgViewer)
find_package(osgEarth CONFIG REQUIRED)

add_executable(osgearth_spike main.cpp OsgWidget.cpp OsgWidget.h)
target_include_directories(osgearth_spike PRIVATE ${OPENSCENEGRAPH_INCLUDE_DIRS})
target_link_libraries(osgearth_spike PRIVATE
    Qt6::Widgets Qt6::OpenGLWidgets
    ${OPENSCENEGRAPH_LIBRARIES}
    osgEarth::osgEarth)

# Copy the earth file next to the exe.
add_custom_command(TARGET osgearth_spike POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_CURRENT_SOURCE_DIR}/spike.earth $<TARGET_FILE_DIR:osgearth_spike>/spike.earth)
