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
# Install
# ============================================================
install(TARGETS client server DESTINATION bin)
install(DIRECTORY include/ DESTINATION include)
