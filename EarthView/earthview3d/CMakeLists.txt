# earthview3d — host-independent osgEarth 3D / stealth view (roadmap E1).
#
# Targets:
#   earthview3d_core  GL-free logic (MapConfig, EntityDiff); Qt Core only, always built.
#   earthview3d       GlobeView3D widget; needs osgEarth (vcpkg manifest at the repo root).
#                     Built when EARTHVIEW_BUILD_3D is ON (default: ON if osgEarth is found).
#
# Consuming (roadmap E7): add_subdirectory(EarthView/earthview3d), or `cmake --install` and
# find_package(earthview3d CONFIG) -> earthview3d::core, earthview3d::earthview3d. Both ways
# provide earthview3d_deploy_runtime(<exe> [INSTALL]) (cmake/earthview3dDeploy.cmake).
#   (The demo host app, earthview3d_demo, lives in demo/ and is added by EarthView/CMakeLists.txt.)
#
# Public headers (include/earthview3d/) contain only Qt/STL types. osgEarth/OSG include
# directories stay PRIVATE so vcpkg's include tree never reaches host code (D-011).

cmake_minimum_required(VERSION 3.21)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/earthview3dDeploy.cmake")
set(EARTHVIEW3D_VERSION 0.7.0)
set(EARTHVIEW3D_CMAKE_INSTALL_DIR "${CMAKE_INSTALL_LIBDIR}/cmake/earthview3d")

find_package(Qt6 REQUIRED COMPONENTS Core Widgets OpenGLWidgets)

add_library(earthview3d_core STATIC
    include/earthview3d/CameraControl.h
    include/earthview3d/EarthFileCatalog.h
    include/earthview3d/EntityState3D.h
    include/earthview3d/EntityDiff.h
    include/earthview3d/MapConfig.h
    include/earthview3d/ModelTable.h
    include/earthview3d/Picking.h
    include/earthview3d/RuntimePaths.h
    include/earthview3d/ScreenScale.h
    include/earthview3d/TerrainServices.h
    src/CameraControl.cpp
    src/EarthFileCatalog.cpp
    src/EntityDiff.cpp
    src/MapConfig.cpp
    src/ModelTable.cpp
    src/Picking.cpp
    src/RuntimePaths.cpp
    src/ScreenScale.cpp
    src/TerrainServices.cpp
)
add_library(earthview3d::core ALIAS earthview3d_core)
target_include_directories(earthview3d_core PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
    "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>")
target_link_libraries(earthview3d_core PUBLIC Qt6::Core)
target_compile_features(earthview3d_core PUBLIC cxx_std_17)
set_target_properties(earthview3d_core PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF EXPORT_NAME core)

# Build-tree runtime defaults (RuntimePaths::buildTreeDefaults): the vcpkg OSG plugin folder
# and GDAL/PROJ data of this configuration. Deployed hosts pass their own paths (E7).
if(DEFINED VCPKG_INSTALLED_DIR AND DEFINED VCPKG_TARGET_TRIPLET)
    set(_vcpkg_root "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}")
    file(GLOB _osg_plugin_dirs LIST_DIRECTORIES true "${_vcpkg_root}/plugins/osgPlugins-*")
    if(_osg_plugin_dirs)
        set(EARTHVIEW3D_VCPKG_ROOT "${_vcpkg_root}")
        target_compile_definitions(earthview3d_core PRIVATE
            EARTHVIEW3D_DEV_OSG_PLUGIN_DIR="${_vcpkg_root}/plugins"
            EARTHVIEW3D_DEV_SHARE_DIR="${_vcpkg_root}/share")
    endif()
endif()

# --- osgEarth part -----------------------------------------------------------------------
find_package(OpenSceneGraph QUIET COMPONENTS osgDB osgGA osgUtil osgViewer)
find_package(osgEarth CONFIG QUIET)
if(OPENSCENEGRAPH_FOUND AND osgEarth_FOUND)
    set(_earthview3d_default ON)
else()
    set(_earthview3d_default OFF)
endif()
option(EARTHVIEW_BUILD_3D "Build the osgEarth-based earthview3d library" ${_earthview3d_default})

# Package export (E7). The 3D target joins the export set further down when it is built.
set(_earthview3d_export_targets earthview3d_core)
set(EARTHVIEW3D_HAS_3D OFF)
set(EARTHVIEW3D_WITH_GDAL OFF)
macro(_earthview3d_install_package)
    install(TARGETS ${_earthview3d_export_targets} EXPORT earthview3dTargets
            ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
            LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
            RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
    install(DIRECTORY include/earthview3d DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
    install(EXPORT earthview3dTargets NAMESPACE earthview3d::
            DESTINATION "${EARTHVIEW3D_CMAKE_INSTALL_DIR}")
    configure_package_config_file(cmake/earthview3dConfig.cmake.in
        "${CMAKE_CURRENT_BINARY_DIR}/earthview3dConfig.cmake"
        INSTALL_DESTINATION "${EARTHVIEW3D_CMAKE_INSTALL_DIR}")
    write_basic_package_version_file("${CMAKE_CURRENT_BINARY_DIR}/earthview3dConfigVersion.cmake"
        VERSION ${EARTHVIEW3D_VERSION} COMPATIBILITY SameMinorVersion)
    install(FILES
        "${CMAKE_CURRENT_BINARY_DIR}/earthview3dConfig.cmake"
        "${CMAKE_CURRENT_BINARY_DIR}/earthview3dConfigVersion.cmake"
        cmake/earthview3dDeploy.cmake
        DESTINATION "${EARTHVIEW3D_CMAKE_INSTALL_DIR}")
endmacro()

if(NOT EARTHVIEW_BUILD_3D)
    message(STATUS "earthview3d: osgEarth part disabled (EARTHVIEW_BUILD_3D=OFF; osgEarth found: ${_earthview3d_default})")
    _earthview3d_install_package()
    return()
endif()
if(NOT (OPENSCENEGRAPH_FOUND AND osgEarth_FOUND))
    message(FATAL_ERROR "EARTHVIEW_BUILD_3D is ON but OpenSceneGraph/osgEarth were not found. "
                        "Configure with the vcpkg toolchain (vcpkg.json at the repo root).")
endif()
message(STATUS "earthview3d: building with osgEarth ${osgEarth_VERSION}")

add_library(earthview3d STATIC
    include/earthview3d/GlobeView3D.h
    include/earthview3d/Runtime.h
    src/GlobeView3D.cpp
    src/Runtime.cpp
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
# GDAL comes with osgEarth (vcpkg); used to set GDAL_DATA/PROJ paths at runtime.
find_package(GDAL CONFIG QUIET)
if(TARGET GDAL::GDAL)
    target_link_libraries(earthview3d PRIVATE GDAL::GDAL)
    target_compile_definitions(earthview3d PRIVATE EARTHVIEW3D_HAVE_GDAL)
    set(EARTHVIEW3D_WITH_GDAL ON)
endif()
if(MSVC)
    # Keep windows.h min/max macros away from std::min/std::max in OSG/osgEarth headers.
    target_compile_definitions(earthview3d PRIVATE NOMINMAX)
endif()

set_target_properties(earthview3d PROPERTIES EXPORT_NAME earthview3d)
list(APPEND _earthview3d_export_targets earthview3d)
set(EARTHVIEW3D_HAS_3D ON)
_earthview3d_install_package()
