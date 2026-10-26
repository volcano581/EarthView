# earthview3d package (roadmap E7): find_package(earthview3d CONFIG)
#   earthview3d::core         GL-free logic (Qt Core)
#   earthview3d::earthview3d  GlobeView3D widget (osgEarth); present when EARTHVIEW3D_HAS_3D
#   earthview3d_deploy_runtime(<exe> [INSTALL] [DESTINATION <dir>] [NO_QT])
#
# earthview3d is a static library, so its private osgEarth/OSG/GDAL link dependencies are
# found here as well; configure the host with the same vcpkg toolchain and triplet.


####### Expanded from @PACKAGE_INIT@ by configure_package_config_file() #######
####### Any changes to this file will be overwritten by the next CMake run ####
####### The input file was earthview3dConfig.cmake.in                            ########

get_filename_component(PACKAGE_PREFIX_DIR "${CMAKE_CURRENT_LIST_DIR}/../../../" ABSOLUTE)

macro(set_and_check _var _file)
  set(${_var} "${_file}")
  if(NOT EXISTS "${_file}")
    message(FATAL_ERROR "File or directory ${_file} referenced by variable ${_var} does not exist !")
  endif()
endmacro()

macro(check_required_components _NAME)
  foreach(comp ${${_NAME}_FIND_COMPONENTS})
    if(NOT ${_NAME}_${comp}_FOUND)
      if(${_NAME}_FIND_REQUIRED_${comp})
        set(${_NAME}_FOUND FALSE)
      endif()
    endif()
  endforeach()
endmacro()

####################################################################################

include(CMakeFindDependencyMacro)

set(EARTHVIEW3D_HAS_3D OFF)
set(EARTHVIEW3D_WITH_GDAL OFF)
set(EARTHVIEW3D_VCPKG_ROOT "")

find_dependency(Qt6 COMPONENTS Core)
if(EARTHVIEW3D_HAS_3D)
    find_dependency(Qt6 COMPONENTS Widgets OpenGLWidgets)
    find_dependency(OpenSceneGraph COMPONENTS osgDB osgGA osgUtil osgViewer)
    find_dependency(osgEarth CONFIG)
    if(EARTHVIEW3D_WITH_GDAL)
        find_dependency(GDAL CONFIG)
    endif()
endif()

include("${CMAKE_CURRENT_LIST_DIR}/earthview3dTargets.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/earthview3dDeploy.cmake")

check_required_components(earthview3d)
