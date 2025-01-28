project(shoc)

cmake_minimum_required(VERSION 3.22)

set(CMAKE_CXX_STANDARD 20)
# Remove when DOCA base images come with gcc > 11 preinstalled
set(CMAKE_CXX_COMPILER g++-12)
set(CMAKE_CC_COMPILER gcc-12)

add_compile_options(-Wall -Wextra -Werror)

find_package(PkgConfig REQUIRED)
pkg_check_modules(
    DOCA 
    REQUIRED
    doca-aes-gcm
    doca-common
    doca-compress
    doca-comch
    doca-dma
    doca-rdma)

file(READ $ENV{HOME}/.vcpkg/vcpkg.path.txt VCPKG_DIR)
include(${VCPKG_DIR}/scripts/buildsystems/vcpkg.cmake)

find_package(asio REQUIRED)
find_package(fmt REQUIRED)
find_package(spdlog REQUIRED)

add_definitions(${DOCA_CFLAGS} -DDOCA_ALLOW_EXPERIMENTAL_API)
include_directories(. ${DOCA_INCLUDE_DIRS})
link_directories(${DOCA_LIBRARY_DIRS})

add_library(
    shoc
    STATIC
    shoc/aes_gcm.cpp
    shoc/buffer.cpp
    shoc/buffer_inventory.cpp
    shoc/buffer_pool.cpp
    shoc/comch/client.cpp
    shoc/comch/consumer.cpp
    shoc/comch/producer.cpp
    shoc/comch/server.cpp
    shoc/compress.cpp
    shoc/context.cpp
    shoc/device.cpp
    shoc/dma.cpp
    shoc/epoll_handle.cpp
    shoc/event_sources.cpp
    shoc/logger.cpp
    shoc/memory_map.cpp
    shoc/progress_engine.cpp
    shoc/rdma.cpp
    shoc/sync_event.cpp
)

target_link_libraries(
    shoc 
    PUBLIC
    asio::asio
    spdlog::spdlog
    fmt::fmt
    ${DOCA_LIBRARIES}
)

add_executable(comch_client progs/comch_client.cpp)
target_link_libraries(comch_client shoc)
