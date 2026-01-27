cmake_minimum_required(VERSION 3.10)

project(opcua_binary_sensors C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

find_package(open62541 REQUIRED)

add_executable(opcua_server
    src/opcua_server.c
)

target_link_libraries(opcua_server
    PRIVATE
        open62541::open62541
)

add_executable(opcua_tcp_bridge
    src/opcua_tcp_bridge.c
)

target_link_libraries(opcua_tcp_bridge
    PRIVATE
        open62541::open62541
)

