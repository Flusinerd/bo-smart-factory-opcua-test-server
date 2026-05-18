cmake_minimum_required(VERSION 3.14)

project(opcua_binary_sensors C CXX)

set(CMAKE_AUTOMOC ON)
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(open62541 REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(MHD REQUIRED IMPORTED_TARGET libmicrohttpd)
pkg_check_modules(CJSON REQUIRED IMPORTED_TARGET libcjson)

find_package(Qt6 COMPONENTS Widgets Network QUIET)

include(FetchContent)
set(MSGPACK_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    msgpackc
    GIT_REPOSITORY https://github.com/msgpack/msgpack-c.git
    GIT_TAG        c_master
)
FetchContent_MakeAvailable(msgpackc)

add_executable(opcua_server
    src/opcua_server.c
    src/server_config.c
    src/node_manager.c
    src/http_server.c
)

target_link_libraries(opcua_server
    PRIVATE
        open62541::open62541
        PkgConfig::MHD
        PkgConfig::CJSON
        pthread
)

add_executable(opcua_tcp_bridge
    src/opcua_tcp_bridge.c
)
target_include_directories(opcua_tcp_bridge PRIVATE ${msgpackc_SOURCE_DIR}/include)
target_link_libraries(opcua_tcp_bridge
    PRIVATE
        open62541::open62541
        msgpack-c
)

if(Qt6_FOUND)
add_executable(opcua_tcp_bridge_gui
    src/gui/main.cpp
    src/gui/MainWindow.cpp
    src/gui/MainWindow.h
    src/gui/BridgeEngine.cpp
    src/gui/BridgeEngine.h
    src/gui/SensorMapping.cpp
    src/gui/SensorMapping.h
    src/gui/OpcUaBrowser.cpp
    src/gui/OpcUaBrowser.h
    src/gui/NodeSelectionDialog.cpp
    src/gui/NodeSelectionDialog.h
)
target_include_directories(opcua_tcp_bridge_gui PRIVATE
    ${msgpackc_SOURCE_DIR}/include
)
target_link_libraries(opcua_tcp_bridge_gui
    PRIVATE
        Qt6::Widgets
        Qt6::Network
        open62541::open62541
        msgpack-c
)
else()
message(STATUS "Qt6 not found - opcua_tcp_bridge_gui will not be built. Install Qt6 to enable the GUI.")
endif()
