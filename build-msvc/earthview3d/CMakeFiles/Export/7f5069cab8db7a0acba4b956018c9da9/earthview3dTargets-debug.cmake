#----------------------------------------------------------------
# Generated CMake target import file for configuration "Debug".
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "earthview3d::core" for configuration "Debug"
set_property(TARGET earthview3d::core APPEND PROPERTY IMPORTED_CONFIGURATIONS DEBUG)
set_target_properties(earthview3d::core PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_DEBUG "CXX"
  IMPORTED_LOCATION_DEBUG "${_IMPORT_PREFIX}/lib/earthview3d_core.lib"
  )

list(APPEND _cmake_import_check_targets earthview3d::core )
list(APPEND _cmake_import_check_files_for_earthview3d::core "${_IMPORT_PREFIX}/lib/earthview3d_core.lib" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)
