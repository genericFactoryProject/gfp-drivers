cmake_minimum_required(VERSION 3.14 FATAL_ERROR)

# ---- Cross Compiler ----

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)
set(CMAKE_C_COMPILER "aarch64-linux-gnu-gcc")
set(CMAKE_CXX_COMPILER "aarch64-linux-gnu-g++")

# ---- Project ----

project(GF LANGUAGES C)

# ---- Create executable ----
set(CMAKE_BUILD_TYPE "Debug")
set(CMAKE_C_FLAGS_DEBUG "$ENV{CMAKE_CXX_FLAGS} -O0 -Wall -g -Wno-pointer-sign -Wno-unused-function -Wno-unused-variable -Wno-unused-but-set-variable -Wno-unused-value -Wno-overflow")

# ---- Arch executable ----
set(ARCH "arm64")

# ---- CONFIG_* ----
add_definitions(-D__KERNEL__="1")

# add your source files here

file(GLOB_RECURSE sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/../kernel/src/*.c"
                                            "${CMAKE_CURRENT_SOURCE_DIR}/../lib/src/*.c")
file(GLOB_RECURSE headers CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/../kernel/include/*.h")
file(GLOB_RECURSE headers CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/../kernel/src/driver/common/include/*.h")
file(GLOB_RECURSE headers CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/../kernel/src/*.h")
file(GLOB_RECURSE headers CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/../lib/include/*.h")
file(GLOB_RECURSE headers CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/../lib/src/*.h")

add_library(gf ${headers} ${sources})
target_include_directories(
  gf PUBLIC $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/../lib/include>
            $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/../lib/include/linux>
            $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/../lib/arch/${ARCH}/include>
            $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/../kernel/include>
            $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/../kernel/include/linux>
            $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/../kernel/src/driver/common/include>
            $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/../kernel/arch/${ARCH}/include>
            $<INSTALL_INTERFACE:include/gf-${PROJECT_VERSION}>
)

# create executable program
# add_executable(main ${sources})

# link to your dependencies' targets here
# target_link_libraries(main gf)

# setup your target's properties
# set_target_properties(main PROPERTIES C_STANDARD 11)
