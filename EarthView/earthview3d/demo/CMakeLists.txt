# earthview3d_demo — host simulator for earthview3d (roadmap E2): EarthView's 2D map plus an
# "Open 3D View" action, fed by a scripted entity feed the way Doctrine feeds its views.
# Added from EarthView/CMakeLists.txt (it reuses the 2D sources), not from earthview3d/.

# GL-free feed (always built; unit tested in tests/tst_syntheticfeed).
add_library(earthview3d_demo_feed STATIC SyntheticFeed.h SyntheticFeed.cpp)
target_include_directories(earthview3d_demo_feed PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(earthview3d_demo_feed PUBLIC earthview3d_core earthview_scene3d Qt6::Core)
set_target_properties(earthview3d_demo_feed PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)

if(NOT TARGET earthview3d)
    return()
endif()

# The 2D map: EarthView's sources without its own main window.
set(_demo_2d_sources ${SOURCES} ${HEADERS})
list(REMOVE_ITEM _demo_2d_sources main.cpp MainWindow.cpp MapViewWindow.cpp MainWindow.h MapViewWindow.h)
list(TRANSFORM _demo_2d_sources PREPEND "${EarthView_SOURCE_DIR}/")

add_executable(earthview3d_demo
    main.cpp
    DemoWindow.h
    DemoWindow.cpp
    ${_demo_2d_sources}
)
target_include_directories(earthview3d_demo PRIVATE "${EarthView_SOURCE_DIR}")
get_target_property(_earthview_2d_libs EarthView LINK_LIBRARIES)
target_link_libraries(earthview3d_demo PRIVATE earthview3d earthview3d_demo_feed ${_earthview_2d_libs})
get_filename_component(_demo_data_root "${EarthView_SOURCE_DIR}/Data" ABSOLUTE)
target_compile_definitions(earthview3d_demo PRIVATE EARTHVIEW3D_DEFAULT_DATA_ROOT="${_demo_data_root}")
add_custom_command(TARGET earthview3d_demo POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${EarthView_SOURCE_DIR}/shaders" "$<TARGET_FILE_DIR:earthview3d_demo>/shaders")
