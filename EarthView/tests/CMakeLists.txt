# Unit tests (Qt Test). Each test links only the sources it exercises.

# The 2D map-source reader: Qt Network (tilemapresource.xml over HTTP) and MbTilesReader for
# the .mbtiles sources. TMSLoader is header-only here (TileSourceLayer is a plain struct).
# Host contract (E8): stands in for Doctrine's adapter and pins the public API with static
# assertions. It includes GlobeView3D.h but never links earthview3d — the public headers are
# osgEarth-free (D-011), and decltype does not odr-use anything — so API drift breaks the
# Linux job too, where the osgEarth widget is not built.
add_executable(tst_hostcontract tst_hostcontract.cpp)
target_link_libraries(tst_hostcontract PRIVATE earthview3d_core Qt6::Core Qt6::Widgets
                                               Qt6::OpenGLWidgets Qt6::Test)
add_test(NAME tst_hostcontract COMMAND tst_hostcontract)

add_executable(tst_mapsourcecatalog
    tst_mapsourcecatalog.cpp
    ../MapSourceCatalog.cpp
    ../MbTilesReader.cpp
)
target_include_directories(tst_mapsourcecatalog PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/..")
get_target_property(_earthview_2d_libs EarthView LINK_LIBRARIES)
target_link_libraries(tst_mapsourcecatalog PRIVATE ${_earthview_2d_libs} Qt6::Test)
add_test(NAME tst_mapsourcecatalog COMMAND tst_mapsourcecatalog)

# The 2D camera's projections (E9 removed its pseudo-3D ones; these must not move).
add_executable(tst_camera2d
    tst_camera2d.cpp
    ../Camera.cpp
    ../MercatorProjection.cpp
)
target_include_directories(tst_camera2d PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_link_libraries(tst_camera2d PRIVATE Qt6::Core Qt6::Gui Qt6::Test)
add_test(NAME tst_camera2d COMMAND tst_camera2d)

add_executable(tst_mercatorprojection
    tst_mercatorprojection.cpp
    ../MercatorProjection.cpp
)
target_include_directories(tst_mercatorprojection PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_link_libraries(tst_mercatorprojection PRIVATE Qt6::Core Qt6::Test)
add_test(NAME tst_mercatorprojection COMMAND tst_mercatorprojection)

# On-map controls state (no Qt Quick needed): the model plus the 2D Camera it syncs with.
add_executable(tst_mapcontrolsmodel
    tst_mapcontrolsmodel.cpp
    ../mapcontrols/MapControlsModel.cpp
    ../mapcontrols/MapControlsModel.h
)
target_include_directories(tst_mapcontrolsmodel PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/.." "${CMAKE_CURRENT_SOURCE_DIR}/../mapcontrols")
target_link_libraries(tst_mapcontrolsmodel PRIVATE earthview::core Qt6::Test)
add_test(NAME tst_mapcontrolsmodel COMMAND tst_mapcontrolsmodel)

# Which map source backs the layers panel's OSM / Imagery tiles.
add_executable(tst_basemapsources
    tst_basemapsources.cpp
    ../mapcontrols/BaseMapSources.cpp
    ../mapcontrols/BaseMapSources.h
    ../mapcontrols/MapControlsModel.cpp
    ../mapcontrols/MapControlsModel.h
)
target_include_directories(tst_basemapsources PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/.." "${CMAKE_CURRENT_SOURCE_DIR}/../mapcontrols")
target_link_libraries(tst_basemapsources PRIVATE earthview::core Qt6::Test)
add_test(NAME tst_basemapsources COMMAND tst_basemapsources)

foreach(test_name tst_geodesy tst_camera3d)
    add_executable(${test_name} ${test_name}.cpp)
    target_link_libraries(${test_name} PRIVATE earthview_scene3d Qt6::Core Qt6::Test)
    add_test(NAME ${test_name} COMMAND ${test_name})
endforeach()

foreach(test_name tst_mapconfig tst_entitydiff tst_terrainservices tst_earthfilecatalog tst_screenscale
                 tst_dtedmosaic)
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
