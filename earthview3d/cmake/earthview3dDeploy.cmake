# earthview3d_deploy_runtime(<target> [INSTALL] [DESTINATION <dir>] [NO_QT])
#
# Makes <target> (a host executable linking earthview3d) runnable without vcpkg on PATH
# (roadmap E7). After each build it copies next to the executable:
#   *.dll from the vcpkg bin folder       osgEarth, OSG, GDAL, PROJ and their dependencies
#   osgPlugins-<version>/                 OSG + osgEarth plugins (incl. vdatum_egm96, freetype)
#   share/gdal, share/proj                GDAL_DATA, PROJ_DATA (proj.db)
# earthview3d::RuntimePaths::defaults() finds this layout itself, so no host code and no
# environment variables are needed.
#
# INSTALL also adds install() rules: the executable and the files above under DESTINATION
# (default "bin") and <prefix>/share, the Qt runtime (Qt's deploy script, unless NO_QT) and
# the MSVC runtime (InstallRequiredSystemLibraries). `cmake --install` then produces a
# self-contained folder for an offline machine.
#
# The vcpkg tree is taken from VCPKG_INSTALLED_DIR/VCPKG_TARGET_TRIPLET (the vcpkg toolchain
# defines both), else from EARTHVIEW3D_VCPKG_ROOT (recorded when earthview3d was built).
# Release only: vcpkg's debug DLLs (debug/bin) are not deployed.

include_guard(GLOBAL)

function(earthview3d_deploy_runtime target)
    cmake_parse_arguments(PARSE_ARGV 1 arg "INSTALL;NO_QT" "DESTINATION" "")
    if(NOT TARGET ${target})
        message(FATAL_ERROR "earthview3d_deploy_runtime: '${target}' is not a target")
    endif()
    if(NOT arg_DESTINATION)
        set(arg_DESTINATION bin)
    endif()

    if(DEFINED VCPKG_INSTALLED_DIR AND DEFINED VCPKG_TARGET_TRIPLET)
        set(_root "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}")
    elseif(EARTHVIEW3D_VCPKG_ROOT)
        set(_root "${EARTHVIEW3D_VCPKG_ROOT}")
    else()
        message(WARNING "earthview3d_deploy_runtime(${target}): no vcpkg tree (configure with the "
                        "vcpkg toolchain); OSG plugins and GDAL/PROJ data are not deployed.")
        return()
    endif()

    file(GLOB _plugin_dirs LIST_DIRECTORIES true "${_root}/plugins/osgPlugins-*")
    if(NOT _plugin_dirs)
        message(WARNING "earthview3d_deploy_runtime(${target}): no osgPlugins-* under ${_root}/plugins; "
                        ".earth files will not load.")
    endif()
    file(GLOB _dlls "${_root}/bin/*.dll")
    set(_data_dirs)
    foreach(_name gdal proj)
        if(IS_DIRECTORY "${_root}/share/${_name}")
            list(APPEND _data_dirs "${_name}")
        endif()
    endforeach()

    # --- build tree ----------------------------------------------------------------------
    set(_copy_dir copy_directory)
    if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.26)
        set(_copy_dir copy_directory_if_different)   # incremental builds stay fast
    endif()
    set(_out "$<TARGET_FILE_DIR:${target}>")
    set(_commands)
    if(_dlls)
        list(APPEND _commands COMMAND ${CMAKE_COMMAND} -E copy_if_different ${_dlls} "${_out}")
    endif()
    foreach(_dir IN LISTS _plugin_dirs)
        get_filename_component(_dir_name "${_dir}" NAME)
        list(APPEND _commands COMMAND ${CMAKE_COMMAND} -E ${_copy_dir} "${_dir}" "${_out}/${_dir_name}")
    endforeach()
    foreach(_name IN LISTS _data_dirs)
        list(APPEND _commands COMMAND ${CMAKE_COMMAND} -E ${_copy_dir}
                              "${_root}/share/${_name}" "${_out}/share/${_name}")
    endforeach()
    if(_commands)
        add_custom_command(TARGET ${target} POST_BUILD ${_commands}
            COMMENT "earthview3d: deploying OSG plugins and GDAL/PROJ data for ${target}"
            VERBATIM)
    endif()

    if(NOT arg_INSTALL)
        return()
    endif()

    # --- install tree --------------------------------------------------------------------
    install(TARGETS ${target} RUNTIME DESTINATION "${arg_DESTINATION}")
    if(_dlls)
        install(FILES ${_dlls} DESTINATION "${arg_DESTINATION}")
    endif()
    foreach(_dir IN LISTS _plugin_dirs)
        install(DIRECTORY "${_dir}" DESTINATION "${arg_DESTINATION}"
                FILES_MATCHING PATTERN "*.dll" PATTERN "*.so")
    endforeach()
    foreach(_name IN LISTS _data_dirs)
        install(DIRECTORY "${_root}/share/${_name}" DESTINATION share
                PATTERN "*.cmake" EXCLUDE
                PATTERN "vcpkg*" EXCLUDE
                PATTERN "copyright" EXCLUDE)
    endforeach()

    if(NOT arg_NO_QT AND COMMAND qt_generate_deploy_app_script)
        qt_generate_deploy_app_script(TARGET ${target} OUTPUT_SCRIPT _qt_deploy_script
                                      NO_UNSUPPORTED_PLATFORM_ERROR)
        install(SCRIPT "${_qt_deploy_script}")
    endif()

    if(MSVC)
        set(CMAKE_INSTALL_SYSTEM_RUNTIME_DESTINATION "${arg_DESTINATION}")
        set(CMAKE_INSTALL_UCRT_LIBRARIES ON)
        include(InstallRequiredSystemLibraries)
    endif()
endfunction()
