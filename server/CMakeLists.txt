cmake_minimum_required(VERSION 3.15)
project(chat_server)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(WIN32)
    add_definitions(-D_WIN32_WINNT=0x0601)
endif()

set(Boost_USE_MULTITHREADED ON)
find_package(Boost 1.75 REQUIRED 
    COMPONENTS 
    system 
    thread 
    json
    url
)

find_library(CRYPTOPP_LIBRARY NAMES cryptopp libcryptopp)
if(NOT CRYPTOPP_LIBRARY)
    message(FATAL_ERROR "Библиотека Crypto++ не найдена")
endif()

add_library(sqlite3 STATIC sqlite/sqlite3.c)
target_include_directories(sqlite3 PUBLIC sqlite)

set(SERVER_SOURCES
    http_session.cpp
    listener.cpp
    main.cpp
    parser.cpp
    shared_state.cpp
    subsciber.cpp
    symbol.cpp
    util.cpp
    websocket_session.cpp
)

set(SERVER_HEADERS
    http_session.hpp
    listener.hpp
    parser.hpp
    shared_state.hpp
    subscriber.hpp
    symbol.hpp
    util.hpp
    websocket_session.hpp
)

add_executable(chat_server ${SERVER_SOURCES} ${SERVER_HEADERS})

target_include_directories(chat_server PRIVATE 
    ${Boost_INCLUDE_DIRS}
)

target_link_libraries(chat_server
    PRIVATE
    Boost::system
    Boost::thread
    Boost::json
    Boost::url
    ${CRYPTOPP_LIBRARY}
    sqlite3
    ws2_32
    mswsock 
)

if(UNIX AND NOT APPLE)
    install(TARGETS chat_server DESTINATION bin)
endif()