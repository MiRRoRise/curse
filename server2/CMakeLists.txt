cmake_minimum_required(VERSION 3.12)
project(RTPServer)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Boost 1.66.0 REQUIRED COMPONENTS system)
find_package(Threads REQUIRED)

add_executable(rtp_server
    server.cpp
    main.cpp
)

target_include_directories(rtp_server PRIVATE ${Boost_INCLUDE_DIRS})
target_link_libraries(rtp_server PRIVATE Boost::system Threads::Threads ws2_32)

if(MSVC)
    target_compile_options(rtp_server PRIVATE /W4)
else()
    target_compile_options(rtp_server PRIVATE -Wall -Wextra -pedantic)
endif()