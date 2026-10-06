cmake_minimum_required(VERSION 3.10)
project(FlightSystem VERSION 1.1 LANGUAGES CXX)

# Same language standard as the Makefile
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    add_compile_options(/W4)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
else()
    add_compile_options(-Wall -Wextra -g)
endif()

include_directories(${CMAKE_SOURCE_DIR}/include)

# ============================================================
# Common library
# ============================================================
add_library(common STATIC
    src/common/message_serializer.cpp
    src/common/udp_socket.cpp
    src/common/logger.cpp
)

if(WIN32)
    target_link_libraries(common PUBLIC ws2_32)
endif()

# ============================================================
# Client
# ============================================================
add_executable(client
    src/client/flight_client.cpp
    src/client/client_ui.cpp
    src/client/client_main.cpp
)

target_link_libraries(client common)

# ============================================================
# Server
# ============================================================
add_executable(server
    src/server/flight_server.cpp
    src/server/flight_manager.cpp
    src/server/monitor_manager.cpp
    src/server/request_history.cpp
    src/server/server_main.cpp
)

target_link_libraries(server common)

# ============================================================
# Unit tests (optional: needs Google Test and the tests/ sources)
# ============================================================
option(BUILD_TESTS "Build test executables" ON)

if(BUILD_TESTS AND EXISTS ${CMAKE_SOURCE_DIR}/tests/test_main.cpp)
    find_package(GTest QUIET)

    if(GTEST_FOUND)
        enable_testing()

        add_executable(tests
            tests/test_main.cpp
            tests/test_message_serializer.cpp
            tests/test_flight_manager.cpp
            src/server/flight_manager.cpp
            src/server/monitor_manager.cpp
            src/server/request_history.cpp
        )

        target_link_libraries(tests
            common
            GTest::GTest
            GTest::Main
        )

        add_test(NAME AllTests COMMAND tests)

        message(STATUS "Google Test found, building tests")
    else()
        message(STATUS "Google Test not found, skipping tests")
        message(STATUS "Install with: brew install googletest")
    endif()
endif()

# ============================================================
# Install
# ============================================================
install(TARGETS client server DESTINATION bin)
install(DIRECTORY include/ DESTINATION include)
