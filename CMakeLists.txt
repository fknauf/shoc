cmake_minimum_required(VERSION 3.22)

project(shoc)

set(CMAKE_CXX_STANDARD 20)
# Remove when DOCA base images come with gcc > 11 preinstalled
set(CMAKE_CXX_COMPILER g++-12)
set(CMAKE_CC_COMPILER gcc-12)

add_compile_options(-Wall -Wextra -Werror $<$<COMPILE_LANGUAGE:CXX>:-Wno-mismatched-new-delete>)
#add_compile_options(-fsanitize=address -fsanitize=undefined)
#add_link_options(-fsanitize=address -fsanitize=undefined)

find_package(PkgConfig REQUIRED)
pkg_check_modules(
    DOCA
    REQUIRED
    doca-aes-gcm
    doca-common
    doca-compress
    doca-comch
    doca-devemu
    doca-dma
    doca-erasure-coding
    doca-eth
    doca-flow
    doca-rdma
    doca-sha
)

file(READ $ENV{HOME}/.vcpkg/vcpkg.path.txt VCPKG_DIR)
include(${VCPKG_DIR}/scripts/buildsystems/vcpkg.cmake)

find_package(Boost CONFIG REQUIRED COMPONENTS cobalt container)
find_package(cxxopts CONFIG REQUIRED)
find_package(fmt CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(GTest CONFIG REQUIRED)

add_definitions(${DOCA_CFLAGS} -DDOCA_ALLOW_EXPERIMENTAL_API)
include_directories(. ${DOCA_INCLUDE_DIRS})
link_directories(${DOCA_LIBRARY_DIRS})

add_library(
    shoc
    SHARED
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
    shoc/devemu_pci.cpp
    shoc/dma.cpp
    shoc/erasure_coding.cpp
    shoc/eth_frame.cpp
    shoc/eth_rxq.cpp
    shoc/eth_txq.cpp
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
    Boost::container
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
    tests/group_eth_frame.cpp
    tests/group_sha.cpp
)
target_link_libraries(test-shoc shoc GTest::gtest GTest::gtest_main)

function(add_shoc_demo_executable name)
    add_executable(${name} ${ARGN})
    target_link_libraries(${name} shoc cxxopts::cxxopts)
endfunction()

add_shoc_demo_executable(comch_client            samples/comch_client.cpp)
add_shoc_demo_executable(comch_server            samples/comch_server.cpp)
add_shoc_demo_executable(comch_server_singleshot samples/comch_server_singleshot.cpp)
add_shoc_demo_executable(comch_data_client       samples/comch_data_client.cpp)
add_shoc_demo_executable(comch_data_server       samples/comch_data_server.cpp)
add_shoc_demo_executable(simple_compress         samples/simple_compress.cpp)
add_shoc_demo_executable(parallel_compress       samples/parallel_compress.cpp)
add_shoc_demo_executable(dma_client              samples/dma_client.cpp)
add_shoc_demo_executable(dma_server              samples/dma_server.cpp)
add_shoc_demo_executable(rdma_receive            samples/rdma_receive.cpp)
add_shoc_demo_executable(rdma_send               samples/rdma_send.cpp)
add_shoc_demo_executable(rdma_cm_server          samples/rdma_cm_server.cpp)
add_shoc_demo_executable(rdma_cm_client          samples/rdma_cm_client.cpp)
add_shoc_demo_executable(sync_event_local_pci    samples/sync_event_local_pci.cpp)
add_shoc_demo_executable(sync_event_remote_pci   samples/sync_event_remote_pci.cpp)
add_shoc_demo_executable(encrypt                 samples/encrypt.cpp)
add_shoc_demo_executable(erasure_encode          samples/erasure_encode.cpp)
add_shoc_demo_executable(erasure_recover         samples/erasure_recover.cpp)
add_shoc_demo_executable(flow_geneve_encap       samples/flow/geneve_encap.cpp)
add_shoc_demo_executable(flow_acl                samples/flow/acl.cpp)
add_shoc_demo_executable(flow_add_to_meta        samples/flow/add_to_meta.cpp)
add_shoc_demo_executable(flow_aging              samples/flow/aging.cpp)
add_shoc_demo_executable(eth_rxq_managed         samples/eth_rxq/managed.cpp)
add_shoc_demo_executable(eth_rxq_proto_splitter  samples/eth_rxq/proto_splitter.cpp)
add_shoc_demo_executable(eth_rxq_kernel_fwd      samples/eth_rxq/kernel_fwd.cpp)
add_shoc_demo_executable(eth_txq_send_raw_packet samples/eth_txq/send_raw_packet.cpp)
add_shoc_demo_executable(eth_udp_echo            samples/eth_udp_echo.cpp)
add_shoc_demo_executable(devemu_pci_hotplug      samples/devemu/pci_hotplug.cpp)
add_shoc_demo_executable(devemu_pci_dma_dpu      samples/devemu/pci_dma/dpu.cpp)
add_shoc_demo_executable(devemu_pci_dma_host     samples/devemu/pci_dma/host.cpp)

add_executable(generate_testdata tools/generate_testdata.cpp)
add_shoc_demo_executable(list_devices tools/list_devices.cpp)

function(add_plain_doca_executable name)
    add_executable(${name} ${ARGN})
    target_link_libraries(${name} ${DOCA_LIBRARIES})
endfunction()

add_plain_doca_executable(doca_simple_compress   samples/plain_doca/simple_compress.c)
add_plain_doca_executable(doca_parallel_compress samples/plain_doca/parallel_compress.c)
add_plain_doca_executable(doca_comch_server      samples/plain_doca/comch_server.c)
add_plain_doca_executable(doca_comch_client      samples/plain_doca/comch_client.c)

add_plain_doca_executable(
    doca_comch_data_server
    samples/plain_doca/comch_data_server/connection.c
    samples/plain_doca/comch_data_server/main.c
    samples/plain_doca/comch_data_server/memory.c
    samples/plain_doca/comch_data_server/server.c
)

add_plain_doca_executable(
    doca_comch_data_client
    samples/plain_doca/comch_data_client/client.c
    samples/plain_doca/comch_data_client/consumer.c
    samples/plain_doca/comch_data_client/main.c
    samples/plain_doca/comch_data_client/memory.c
)

add_plain_doca_executable(
    doca_dma_server
    samples/plain_doca/dma_server/main.c
    samples/plain_doca/dma_server/memory.c
    samples/plain_doca/dma_server/server.c
)

add_plain_doca_executable(
    doca_dma_client
    samples/plain_doca/dma_client/comch.c
    samples/plain_doca/dma_client/common.c
    samples/plain_doca/dma_client/dma.c
    samples/plain_doca/dma_client/main.c
)

add_plain_doca_executable(
    bug-comch-consumer-start-lost-server
    bug-reports/comch-consumer-start-lost/server.c
)

add_plain_doca_executable(
    bug-comch-consumer-start-lost-client
    bug-reports/comch-consumer-start-lost/client.c
)
