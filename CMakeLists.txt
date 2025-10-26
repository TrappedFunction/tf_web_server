cmake_minimum_required(VERSION 3.10)
project(tf_web_server)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_BUILD_TYPE Debug)

add_executable(server src/main.cpp src/server.cpp src/socket.cpp
                    src/http_request.cpp src/http_response.cpp
                    src/connection.cpp src/buffer.cpp
                    src/mime_types.cpp)
target_include_directories(server PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)