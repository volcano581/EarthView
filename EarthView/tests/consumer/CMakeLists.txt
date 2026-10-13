# External consumer of the installed earthview3d package (roadmap E7 acceptance).
# Not part of the EarthView build: CI configures it separately against `cmake --install`:
#   cmake -S EarthView/tests/consumer -B consumer -DCMAKE_PREFIX_PATH=<install prefix>;<Qt>
# With the 3D part it also needs the vcpkg toolchain of the earthview3d build.

cmake_minimum_required(VERSION 3.21)
project(earthview3d_consumer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
if(MSVC)
    set(CMAKE_CXX_STANDARD 20)   # Qt 6.11 moc output, as in EarthView/CMakeLists.txt
endif()
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(earthview3d 0.7 CONFIG REQUIRED)

add_executable(consumer_core consumer_core.cpp)
target_link_libraries(consumer_core PRIVATE earthview3d::core)

if(TARGET earthview3d::earthview3d)
    add_executable(consumer_3d consumer_3d.cpp)
    target_link_libraries(consumer_3d PRIVATE earthview3d::earthview3d)
    earthview3d_deploy_runtime(consumer_3d INSTALL)
endif()
