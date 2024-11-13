cmake_minimum_required(VERSION 3.14 FATAL_ERROR)

# ---- Cross Compiler ----

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)
set(CMAKE_C_COMPILER "aarch64-linux-gnu-gcc")
set(CMAKE_CXX_COMPILER "aarch64-linux-gnu-g++")

# ---- Project ----

project(drvmgr LANGUAGES C)

# ---- Create executable ----
set(CMAKE_BUILD_TYPE "Debug")
set(CMAKE_C_FLAGS_DEBUG "$ENV{CMAKE_CXX_FLAGS} -O0 -Wall -g -Wno-pointer-sign")


# ---- CONFIG_* ----
add_definitions(-D__KERNEL__="1")


# add your source files here

file(GLOB_RECURSE headers CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/include/*.h")
file(GLOB_RECURSE headers CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.h")

file(GLOB_RECURSE sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c" "${CMAKE_CURRENT_SOURCE_DIR}/libbase/src/*.c")

file(GLOB_RECURSE headers CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/libbase/include/*.h")
file(GLOB_RECURSE headers CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/libbase/src/*.h")

add_library(drvmgr_lib ${headers} ${sources})
target_include_directories(
  drvmgr_lib PUBLIC $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
                        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include/linux>
                        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/arch/arm64/include>
                        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/libbase/include>
                        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/libbase/include/linux>
                        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/libbase/arch/arm64/include>
                        $<INSTALL_INTERFACE:include/drvmgr_lib-${PROJECT_VERSION}>
)

file(GLOB sources CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/test/src/*.c)

add_executable(drvmgr_bin ${sources})
# link to your dependencies' targets here
target_link_libraries(drvmgr_bin drvmgr_lib)
# setup your target's properties
set_target_properties(drvmgr_bin PROPERTIES C_STANDARD 11)
