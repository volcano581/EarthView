# Unit tests (Qt Test). Each test links only the sources it exercises.

add_executable(tst_mercatorprojection
    tst_mercatorprojection.cpp
    ../MercatorProjection.cpp
)
target_include_directories(tst_mercatorprojection PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_link_libraries(tst_mercatorprojection PRIVATE Qt6::Core Qt6::Test)
add_test(NAME tst_mercatorprojection COMMAND tst_mercatorprojection)

foreach(test_name tst_geodesy tst_camera3d)
    add_executable(${test_name} ${test_name}.cpp)
    target_link_libraries(${test_name} PRIVATE earthview_scene3d Qt6::Core Qt6::Test)
    add_test(NAME ${test_name} COMMAND ${test_name})
endforeach()

foreach(test_name tst_mapconfig tst_entitydiff tst_terrainservices)
    add_executable(${test_name} ${test_name}.cpp)
    target_link_libraries(${test_name} PRIVATE earthview3d_core Qt6::Core Qt6::Test)
    add_test(NAME ${test_name} COMMAND ${test_name})
endforeach()

add_executable(tst_runtimepaths tst_runtimepaths.cpp)
target_link_libraries(tst_runtimepaths PRIVATE earthview3d_core Qt6::Core Qt6::Test)
add_test(NAME tst_runtimepaths COMMAND tst_runtimepaths)

add_executable(tst_syntheticfeed tst_syntheticfeed.cpp)
target_link_libraries(tst_syntheticfeed PRIVATE earthview3d_demo_feed Qt6::Core Qt6::Test)
add_test(NAME tst_syntheticfeed COMMAND tst_syntheticfeed)

add_executable(tst_modeltable tst_modeltable.cpp)
target_link_libraries(tst_modeltable PRIVATE earthview3d_core Qt6::Core Qt6::Test)
add_test(NAME tst_modeltable COMMAND tst_modeltable)

add_executable(tst_cameracontrol tst_cameracontrol.cpp)
target_link_libraries(tst_cameracontrol PRIVATE earthview3d_core earthview_scene3d Qt6::Core Qt6::Test)
add_test(NAME tst_cameracontrol COMMAND tst_cameracontrol)

# Offline data pipeline (E6): Python unittest; GDAL-dependent cases skip without the GDAL tools.
find_package(Python3 COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND)
    add_test(NAME test_build_offline_data
             COMMAND Python3::Interpreter "${CMAKE_CURRENT_SOURCE_DIR}/../tools/offline_data/test_build_offline_data.py")
endif()
