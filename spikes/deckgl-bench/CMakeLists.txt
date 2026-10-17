cmake_minimum_required(VERSION 3.21)
project(deckgl_bench LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets WebEngineWidgets WebChannel)

# deck.gl is vendored locally (offline run, no CDN); see README.md.
if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/web/deck.gl.min.js")
    message(FATAL_ERROR
        "web/deck.gl.min.js is missing. Download the deck.gl UMD bundle into "
        "spikes/deckgl-bench/web/ first (see README.md).")
endif()

add_executable(deckgl_bench main.cpp resources.qrc)
target_link_libraries(deckgl_bench PRIVATE
    Qt6::Widgets Qt6::WebEngineWidgets Qt6::WebChannel)
