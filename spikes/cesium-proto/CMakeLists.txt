cmake_minimum_required(VERSION 3.21)
project(cesium_proto LANGUAGES CXX)

# Qt 6.11's moc output needs C++20 with MSVC (EarthView decision D-016).
if(MSVC)
    set(CMAKE_CXX_STANDARD 20)
else()
    set(CMAKE_CXX_STANDARD 17)
endif()
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Network Sql WebEngineWidgets)

add_executable(cesium_proto
    main.cpp
    HttpServer.cpp HttpServer.h
    Dted.cpp Dted.h
    BaseMap.cpp BaseMap.h
    Sim.cpp Sim.h
)
target_link_libraries(cesium_proto PRIVATE
    Qt6::Widgets Qt6::Network Qt6::Sql Qt6::WebEngineWidgets)

# The page is served straight from the source tree, so web/ edits need only a reload.
target_compile_definitions(cesium_proto PRIVATE
    CESIUM_PROTO_WEB_DIR="${CMAKE_CURRENT_SOURCE_DIR}/web"
    CESIUM_PROTO_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../../EarthView/Data")

if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/web/cesium/Cesium.js")
    message(WARNING "web/cesium/ is missing: vendor the CesiumJS build there before running (see README.md).")
endif()
