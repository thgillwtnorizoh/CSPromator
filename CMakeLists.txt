cmake_minimum_required(VERSION 3.24)
project(CSPromator VERSION 0.0.4 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(cspromator-core STATIC
    src/clock.cpp
    src/session.cpp
    src/game_state.cpp
    src/event_detector.cpp
    src/live_event_pipeline.cpp
)

target_include_directories(cspromator-core PUBLIC include)
target_compile_definitions(cspromator-core PUBLIC NOMINMAX WIN32_LEAN_AND_MEAN)

add_executable(cspromator-probe
    src/main.cpp
    src/http_server.cpp
)
target_link_libraries(cspromator-probe PRIVATE cspromator-core)

if(WIN32)
    target_link_libraries(cspromator-probe PRIVATE ws2_32)
endif()

foreach(target cspromator-core cspromator-probe)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive- /EHsc)
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endforeach()

enable_testing()
add_executable(cspromator-event-tests tests/event_detector_tests.cpp)
target_link_libraries(cspromator-event-tests PRIVATE cspromator-core)
add_test(NAME event-regressions COMMAND cspromator-event-tests)

add_executable(cspromator-session-tests tests/session_storage_tests.cpp)
target_link_libraries(cspromator-session-tests PRIVATE cspromator-core)
add_test(NAME session-storage COMMAND cspromator-session-tests)

add_executable(cspromator-session002-tests tests/session_002_regressions.cpp)
target_link_libraries(cspromator-session002-tests PRIVATE cspromator-core)
add_test(NAME session-002-regressions COMMAND cspromator-session002-tests)

add_executable(cspromator-session003-tests tests/session_003_retakes_regressions.cpp)
target_link_libraries(cspromator-session003-tests PRIVATE cspromator-core)
add_test(NAME session-003-retakes COMMAND cspromator-session003-tests)

add_executable(cspromator-live-pipeline-tests tests/live_event_pipeline_tests.cpp)
target_link_libraries(cspromator-live-pipeline-tests PRIVATE cspromator-core)
add_test(NAME live-event-pipeline COMMAND cspromator-live-pipeline-tests)

install(TARGETS cspromator-probe RUNTIME DESTINATION bin)
