cmake_minimum_required(VERSION 3.22)

if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    file(READ $ENV{HOME}/.vcpkg/vcpkg.path.txt VCPKG_DIR)
    include(${VCPKG_DIR}/scripts/buildsystems/vcpkg.cmake)
endif()

project(shoc)

set(CMAKE_CXX_STANDARD 20)
# Remove when DOCA base images come with gcc > 11 preinstalled
set(CMAKE_CXX_COMPILER g++-12)
set(CMAKE_CC_COMPILER gcc-12)

add_compile_options(-Wall -Wextra -Werror $<$<COMPILE_LANGUAGE:CXX>:-Wno-mismatched-new-delete>)

find_package(PkgConfig REQUIRED)
pkg_check_modules(
    DOCA
    REQUIRED
    doca-aes-gcm
    doca-common
    doca-compress
    doca-comch
    doca-dma
    doca-erasure-coding
    doca-flow
    doca-rdma
    doca-sha
)

find_package(Boost CONFIG REQUIRED COMPONENTS cobalt)
find_package(cxxopts CONFIG REQUIRED)
find_package(fmt CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(GTest CONFIG REQUIRED)

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
    shoc/erasure_coding.cpp
    shoc/flow.cpp
    shoc/logger.cpp
    shoc/memory_map.cpp
    shoc/progress_engine.cpp
    shoc/rdma.cpp
    shoc/sha.cpp
    shoc/sync_event.cpp
)

target_link_libraries(
    shoc
    PUBLIC
    Boost::cobalt
    spdlog::spdlog
    fmt::fmt
    ${DOCA_LIBRARIES}
)

add_executable(test-shoc
    tests/coro/group_value_awaitable.cpp
    tests/group_aes_gcm.cpp
    tests/group_aligned_mem.cpp
    tests/group_compress.cpp
    tests/group_dma.cpp
    tests/group_engine.cpp
    tests/group_erasure_coding.cpp
    tests/group_sha.cpp
)
target_link_libraries(test-shoc shoc GTest::gtest GTest::gtest_main)

function(add_shoc_demo_executable name)
    add_executable(${name} ${ARGN})
    target_link_libraries(${name} shoc cxxopts::cxxopts)
endfunction()

add_shoc_demo_executable(comch_client            progs/comch_client.cpp)
add_shoc_demo_executable(comch_server            progs/comch_server.cpp)
add_shoc_demo_executable(comch_server_singleshot progs/comch_server_singleshot.cpp)
add_shoc_demo_executable(comch_data_client       progs/comch_data_client.cpp)
add_shoc_demo_executable(comch_data_server       progs/comch_data_server.cpp)
add_shoc_demo_executable(simple_compress         progs/simple_compress.cpp)
add_shoc_demo_executable(parallel_compress       progs/parallel_compress.cpp)
add_shoc_demo_executable(dma_client              progs/dma_client.cpp)
add_shoc_demo_executable(dma_server              progs/dma_server.cpp)
add_shoc_demo_executable(rdma_receive            progs/rdma_receive.cpp)
add_shoc_demo_executable(rdma_send               progs/rdma_send.cpp)
add_shoc_demo_executable(rdma_cm_server          progs/rdma_cm_server.cpp)
add_shoc_demo_executable(rdma_cm_client          progs/rdma_cm_client.cpp)
add_shoc_demo_executable(sync_event_local_pci    progs/sync_event_local_pci.cpp)
add_shoc_demo_executable(sync_event_remote_pci   progs/sync_event_remote_pci.cpp)
add_shoc_demo_executable(encrypt                 progs/encrypt.cpp)
add_shoc_demo_executable(erasure_encode          progs/erasure_encode.cpp)
add_shoc_demo_executable(erasure_recover         progs/erasure_recover.cpp)
add_shoc_demo_executable(flow_geneve_encap       progs/flow/geneve_encap.cpp)
add_shoc_demo_executable(flow_acl                progs/flow/acl.cpp)
add_shoc_demo_executable(flow_add_to_meta        progs/flow/add_to_meta.cpp)
add_shoc_demo_executable(flow_aging              progs/flow/aging.cpp)

add_executable(generate_testdata tools/generate_testdata.cpp)

function(add_plain_doca_executable name)
    add_executable(${name} ${ARGN})
    target_link_libraries(${name} ${DOCA_LIBRARIES})
endfunction()

add_plain_doca_executable(doca_simple_compress   progs/plain_doca/simple_compress.c)
add_plain_doca_executable(doca_parallel_compress progs/plain_doca/parallel_compress.c)
add_plain_doca_executable(doca_comch_server      progs/plain_doca/comch_server.c)
add_plain_doca_executable(doca_comch_client      progs/plain_doca/comch_client.c)

add_plain_doca_executable(
    doca_comch_data_server
    progs/plain_doca/comch_data_server/connection.c
    progs/plain_doca/comch_data_server/main.c
    progs/plain_doca/comch_data_server/memory.c
    progs/plain_doca/comch_data_server/server.c
)

add_plain_doca_executable(
    doca_comch_data_client
    progs/plain_doca/comch_data_client/client.c
    progs/plain_doca/comch_data_client/consumer.c
    progs/plain_doca/comch_data_client/main.c
    progs/plain_doca/comch_data_client/memory.c
)

add_plain_doca_executable(
    doca_dma_server
    progs/plain_doca/dma_server/main.c
    progs/plain_doca/dma_server/memory.c
    progs/plain_doca/dma_server/server.c
)

add_plain_doca_executable(
    doca_dma_client
    progs/plain_doca/dma_client/comch.c
    progs/plain_doca/dma_client/common.c
    progs/plain_doca/dma_client/dma.c
    progs/plain_doca/dma_client/main.c
)

add_plain_doca_executable(
    bug-comch-consumer-start-lost-server
    bug-reports/comch-consumer-start-lost/server.c
)

add_plain_doca_executable(
    bug-comch-consumer-start-lost-client
    bug-reports/comch-consumer-start-lost/client.c
)
