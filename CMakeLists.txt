cmake_minimum_required(VERSION 3.16)
project(rpmbd LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Default build type (only relevant for single-config generators)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# Dependencies
find_package(OpenSSL REQUIRED)          # OpenSSL::Crypto
find_package(Threads REQUIRED)

find_package(PkgConfig REQUIRED)
pkg_check_modules(FUSE3 REQUIRED fuse3)

# Sources
file(GLOB_RECURSE SRC_FILES CONFIGURE_DEPENDS
     ${CMAKE_SOURCE_DIR}/src/*.cpp
     ${CMAKE_SOURCE_DIR}/src/*.h)

add_executable(rpmbd ${SRC_FILES})

target_include_directories(rpmbd PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${FUSE3_INCLUDE_DIRS}
)

target_link_directories(rpmbd PRIVATE
  ${FUSE3_LIBRARY_DIRS}
)

target_link_libraries(rpmbd PRIVATE
  OpenSSL::Crypto
  ${FUSE3_LIBRARIES}
  Threads::Threads
)

target_compile_options(rpmbd PRIVATE
  -Wno-deprecated-declarations
  $<$<CONFIG:Release>:-g2>
)

# rpath: allow loading libs next to binary
set_target_properties(rpmbd PROPERTIES
  BUILD_RPATH "\$ORIGIN"
  INSTALL_RPATH "\$ORIGIN"
)

