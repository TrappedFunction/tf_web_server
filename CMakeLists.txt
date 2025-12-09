cmake_minimum_required(VERSION 3.10)
project(tf_web_server)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_BUILD_TYPE Debug)

find_package(OpenSSL REQUIRED)

# 收集源文件
file(GLOB_RECURSE SOURCES 
    "src/net/*.cpp" 
    "src/http/*.cpp" 
    "src/utils/*.cpp" 
    "src/db/core/*.cpp" # **新增：数据库核心源文件**
    "src/main.cpp" "src/buffer.cpp" "src/connection.cpp" "src/http_request.cpp" 
    "src/http_response.cpp" "src/http_utils.cpp" "src/mime_types.cpp" "src/server.cpp" "src/socket.cpp"
)

add_executable(server ${SOURCES})
target_include_directories(server PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_include_directories(server PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src/db/include)
target_link_libraries(server PRIVATE OpenSSL::SSL OpenSSL::Crypto pthread)