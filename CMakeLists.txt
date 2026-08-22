cmake_minimum_required(VERSION 3.24)
project(CSPromator VERSION 0.0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(cspromator-probe
    src/main.cpp
    src/clock.cpp
    src/http_server.cpp
    src/session.cpp
)

target_include_directories(cspromator-probe PRIVATE include)

target_compile_definitions(cspromator-probe PRIVATE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
)

if(MSVC)
    target_compile_options(cspromator-probe PRIVATE /W4 /permissive- /EHsc)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(cspromator-probe PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(WIN32)
    target_link_libraries(cspromator-probe PRIVATE ws2_32)
endif()

install(TARGETS cspromator-probe RUNTIME DESTINATION bin)
