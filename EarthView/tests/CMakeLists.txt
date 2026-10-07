# Unit tests (Qt Test). Each test links only the sources it exercises.

add_executable(tst_mercatorprojection
    tst_mercatorprojection.cpp
    ../MercatorProjection.cpp
)
target_include_directories(tst_mercatorprojection PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_link_libraries(tst_mercatorprojection PRIVATE Qt6::Core Qt6::Test)
add_test(NAME tst_mercatorprojection COMMAND tst_mercatorprojection)
