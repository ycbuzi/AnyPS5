cmake_minimum_required(VERSION 3.20)
project(anyps5_console LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

find_package(SDL3 REQUIRED CONFIG)
find_package(SDL3_ttf REQUIRED CONFIG)
find_package(SDL3_image REQUIRED CONFIG)
find_package(Threads REQUIRED)

find_package(nlohmann_json REQUIRED CONFIG)

file(GLOB_RECURSE APP_SOURCES CONFIGURE_DEPENDS
  ${CMAKE_SOURCE_DIR}/src/core/*.cpp
  ${CMAKE_SOURCE_DIR}/src/host/*.cpp
  ${CMAKE_SOURCE_DIR}/src/widgets/*.cpp
  ${CMAKE_SOURCE_DIR}/src/screens/*.cpp)
if(WIN32)
  file(GLOB PLATFORM_SOURCES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/src/platform/win/*.cpp)
else()
  file(GLOB PLATFORM_SOURCES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/src/platform/posix/*.cpp)
endif()
file(GLOB PLATFORM_COMMON CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/src/platform/*.cpp)

add_executable(anyps5-console src/main.cpp ${APP_SOURCES} ${PLATFORM_SOURCES} ${PLATFORM_COMMON})
target_include_directories(anyps5-console PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(anyps5-console PRIVATE SDL3::SDL3 SDL3_ttf::SDL3_ttf SDL3_image::SDL3_image nlohmann_json::nlohmann_json Threads::Threads)
target_compile_definitions(anyps5-console PRIVATE ANYPS5_SOURCE_ASSETS="${CMAKE_SOURCE_DIR}/assets")
if(WIN32)
  target_link_libraries(anyps5-console PRIVATE ws2_32)
endif()
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
  target_compile_options(anyps5-console PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()
if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64" AND CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
  target_compile_options(anyps5-console PRIVATE -march=x86-64-v2 -mtune=generic)
endif()
add_custom_command(TARGET anyps5-console POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/assets $<TARGET_FILE_DIR:anyps5-console>/assets)
