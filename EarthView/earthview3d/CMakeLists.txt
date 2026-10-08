# earthview3d — host-independent osgEarth 3D / stealth view (roadmap E1).
#
# Targets:
#   earthview3d_core  GL-free logic (MapConfig, EntityDiff); Qt Core only, always built.
#   earthview3d       GlobeView3D widget; needs osgEarth (vcpkg manifest at the repo root).
#                     Built when EARTHVIEW_BUILD_3D is ON (default: ON if osgEarth is found).
#   earthview3d_viewer  test viewer (ported spike app), with EARTHVIEW_BUILD_3D.
#
# Public headers (include/earthview3d/) contain only Qt/STL types. osgEarth/OSG include
# directories stay PRIVATE so vcpkg's include tree never reaches host code (D-011).

cmake_minimum_required(VERSION 3.21)

find_package(Qt6 REQUIRED COMPONENTS Core Widgets OpenGLWidgets)

add_library(earthview3d_core STATIC
    include/earthview3d/EntityState3D.h
    include/earthview3d/EntityDiff.h
    include/earthview3d/MapConfig.h
    src/EntityDiff.cpp
    src/MapConfig.cpp
)
add_library(earthview3d::core ALIAS earthview3d_core)
target_include_directories(earthview3d_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(earthview3d_core PUBLIC Qt6::Core)
target_compile_features(earthview3d_core PUBLIC cxx_std_17)
set_target_properties(earthview3d_core PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)

# --- osgEarth part -----------------------------------------------------------------------
find_package(OpenSceneGraph QUIET COMPONENTS osgDB osgGA osgUtil osgViewer)
find_package(osgEarth CONFIG QUIET)
if(OPENSCENEGRAPH_FOUND AND osgEarth_FOUND)
    set(_earthview3d_default ON)
else()
    set(_earthview3d_default OFF)
endif()
option(EARTHVIEW_BUILD_3D "Build the osgEarth-based earthview3d library" ${_earthview3d_default})

if(NOT EARTHVIEW_BUILD_3D)
    message(STATUS "earthview3d: osgEarth part disabled (EARTHVIEW_BUILD_3D=OFF; osgEarth found: ${_earthview3d_default})")
    return()
endif()
if(NOT (OPENSCENEGRAPH_FOUND AND osgEarth_FOUND))
    message(FATAL_ERROR "EARTHVIEW_BUILD_3D is ON but OpenSceneGraph/osgEarth were not found. "
                        "Configure with the vcpkg toolchain (vcpkg.json at the repo root).")
endif()
message(STATUS "earthview3d: building with osgEarth ${osgEarth_VERSION}")

add_library(earthview3d STATIC
    include/earthview3d/GlobeView3D.h
    src/GlobeView3D.cpp
)
add_library(earthview3d::earthview3d ALIAS earthview3d)
set_target_properties(earthview3d PROPERTIES AUTOMOC ON)
target_link_libraries(earthview3d
    PUBLIC
        earthview3d_core
        Qt6::Widgets
        Qt6::OpenGLWidgets
    PRIVATE
        ${OPENSCENEGRAPH_LIBRARIES}
        osgEarth::osgEarth
)
target_include_directories(earthview3d PRIVATE ${OPENSCENEGRAPH_INCLUDE_DIRS})
if(MSVC)
    # Keep windows.h min/max macros away from std::min/std::max in OSG/osgEarth headers.
    target_compile_definitions(earthview3d PRIVATE NOMINMAX)
endif()

# Test viewer (ported spike). Paths below are for running from the build tree only.
add_executable(earthview3d_viewer examples/earthview3d_viewer.cpp)
target_link_libraries(earthview3d_viewer PRIVATE earthview3d)
set_target_properties(earthview3d_viewer PROPERTIES AUTOMOC ON)
get_filename_component(_earthview3d_data_root "${CMAKE_CURRENT_SOURCE_DIR}/../Data" ABSOLUTE)
target_compile_definitions(earthview3d_viewer PRIVATE
    EARTHVIEW3D_DEFAULT_DATA_ROOT="${_earthview3d_data_root}")
if(DEFINED VCPKG_INSTALLED_DIR AND DEFINED VCPKG_TARGET_TRIPLET)
    set(_vcpkg_root "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}")
    file(GLOB _osg_plugin_dirs LIST_DIRECTORIES true "${_vcpkg_root}/plugins/osgPlugins-*")
    if(_osg_plugin_dirs)
        target_compile_definitions(earthview3d_viewer PRIVATE
            EARTHVIEW3D_DEV_OSG_PLUGIN_DIR="${_vcpkg_root}/plugins"
            EARTHVIEW3D_DEV_SHARE_DIR="${_vcpkg_root}/share")
    endif()
endif()
