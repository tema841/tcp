cmake_minimum_required(VERSION 3.20)
project(adaptive_tcp_transfer LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(MSVC)
  add_compile_options(/W4)
else()
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

add_library(core STATIC
  socket.cpp transfer.cpp sha256.cpp metrics.cpp
)
target_include_directories(core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
if(WIN32)
  target_link_libraries(core PUBLIC ws2_32)
endif()

add_executable(server server.cpp)
target_link_libraries(server PRIVATE core)
add_executable(client client.cpp)
target_link_libraries(client PRIVATE core)
add_executable(tests tests.cpp)
target_link_libraries(tests PRIVATE core)

add_custom_command(TARGET server POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory
          ${CMAKE_CURRENT_SOURCE_DIR}/web
          $<TARGET_FILE_DIR:server>/web)

include(CTest)
add_test(NAME transfer_tests COMMAND tests)
