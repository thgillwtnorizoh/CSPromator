cmake_minimum_required(VERSION 3.24)
project(CSPromator VERSION 0.0.7 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(cspromator-core STATIC
    src/clock.cpp
    src/session.cpp
    src/game_state.cpp
    src/event_detector.cpp
    src/supplementary_state.cpp
    src/semantic_resolver.cpp
    src/live_event_pipeline.cpp
    src/telemetry_ingress.cpp
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

add_executable(cspromator-session004-tests tests/session_004_casual_regressions.cpp)
target_link_libraries(cspromator-session004-tests PRIVATE cspromator-core)
add_test(NAME session-004-casual COMMAND cspromator-session004-tests)

add_executable(cspromator-session005-tests tests/session_005_lifecycle_regressions.cpp)
target_link_libraries(cspromator-session005-tests PRIVATE cspromator-core)
add_test(NAME session-005-lifecycle COMMAND cspromator-session005-tests)

add_executable(cspromator-live-pipeline-tests tests/live_event_pipeline_tests.cpp)
target_link_libraries(cspromator-live-pipeline-tests PRIVATE cspromator-core)
add_test(NAME live-event-pipeline COMMAND cspromator-live-pipeline-tests)

add_executable(cspromator-semantic-resolver-tests tests/semantic_resolver_tests.cpp)
target_link_libraries(cspromator-semantic-resolver-tests PRIVATE cspromator-core)
add_test(NAME semantic-resolver COMMAND cspromator-semantic-resolver-tests)

add_executable(cspromator-live-supplement-tests tests/live_supplementary_pipeline_tests.cpp)
target_link_libraries(cspromator-live-supplement-tests PRIVATE cspromator-core)
add_test(NAME live-supplementary-pipeline COMMAND cspromator-live-supplement-tests)

add_executable(cspromator-stale-supplement-tests tests/stale_supplement_regression_tests.cpp)
target_link_libraries(cspromator-stale-supplement-tests PRIVATE cspromator-core)
add_test(NAME stale-supplement-regression COMMAND cspromator-stale-supplement-tests)

install(TARGETS cspromator-probe RUNTIME DESTINATION bin)
