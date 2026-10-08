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

foreach(test_name tst_mapconfig tst_entitydiff)
    add_executable(${test_name} ${test_name}.cpp)
    target_link_libraries(${test_name} PRIVATE earthview3d_core Qt6::Core Qt6::Test)
    add_test(NAME ${test_name} COMMAND ${test_name})
endforeach()
