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

# ---------------------------------------------------------------------------
# Offline deployment: `cmake --install build --config Release --prefix <dir>`
# produces a self-contained folder (no vcpkg, no network needed):
#   <dir>/bin/    exe, spike.earth, Qt + vcpkg DLLs, osgPlugins-*/
#   <dir>/share/  gdal/ (GDAL_DATA), proj/ (proj.db for PROJ_DATA)
# main.cpp points OSG/GDAL/PROJ at these folders when the env vars are unset.
# Release only: vcpkg's debug DLLs live elsewhere and are not deployed.
# ---------------------------------------------------------------------------
include(GNUInstallDirs)

install(TARGETS osgearth_spike RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES spike.earth DESTINATION ${CMAKE_INSTALL_BINDIR})

# Qt DLLs and plugins (windeployqt under the hood).
qt_generate_deploy_app_script(
    TARGET osgearth_spike
    OUTPUT_SCRIPT qtDeployScript
    NO_UNSUPPORTED_PLATFORM_ERROR)
install(SCRIPT ${qtDeployScript})

# vcpkg runtime: OSG/osgEarth/GDAL/PROJ/etc. DLLs, OSG plugins, GDAL and PROJ data.
if(DEFINED VCPKG_INSTALLED_DIR AND DEFINED VCPKG_TARGET_TRIPLET)
    set(vcpkgRoot "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}")

    install(DIRECTORY "${vcpkgRoot}/bin/" DESTINATION ${CMAKE_INSTALL_BINDIR}
        FILES_MATCHING PATTERN "*.dll")

    file(GLOB osgPluginDirs LIST_DIRECTORIES true "${vcpkgRoot}/plugins/osgPlugins-*")
    if(NOT osgPluginDirs)
        message(WARNING "No osgPlugins-* folder under ${vcpkgRoot}/plugins; the installed app will not load .earth files.")
    endif()
    foreach(pluginDir IN LISTS osgPluginDirs)
        install(DIRECTORY "${pluginDir}" DESTINATION ${CMAKE_INSTALL_BINDIR}
            FILES_MATCHING PATTERN "*.dll")
    endforeach()

    foreach(dataDir gdal proj)
        if(EXISTS "${vcpkgRoot}/share/${dataDir}")
            install(DIRECTORY "${vcpkgRoot}/share/${dataDir}" DESTINATION ${CMAKE_INSTALL_DATADIR}
                PATTERN "*.cmake" EXCLUDE
                PATTERN "vcpkg*" EXCLUDE
                PATTERN "copyright" EXCLUDE)
        endif()
    endforeach()
else()
    message(WARNING "Not configured with the vcpkg toolchain; install() will not deploy OSG/GDAL runtime files.")
endif()
