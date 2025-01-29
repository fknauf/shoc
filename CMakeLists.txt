project(shoc)

cmake_minimum_required(VERSION 3.22)

set(CMAKE_CXX_STANDARD 20)
# Remove when DOCA base images come with gcc > 11 preinstalled
set(CMAKE_CXX_COMPILER g++-12)
set(CMAKE_CC_COMPILER gcc-12)

add_compile_options(-Wall -Wextra -Werror -Wno-mismatched-new-delete)

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

find_package(Boost CONFIG REQUIRED COMPONENTS cobalt)
find_package(fmt REQUIRED)
find_package(spdlog REQUIRED)
find_package(GTest REQUIRED)

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
    Boost::cobalt
    spdlog::spdlog
    fmt::fmt
    ${DOCA_LIBRARIES}
)

add_executable(comch_client progs/comch_client.cpp)
target_link_libraries(comch_client shoc)

add_executable(comch_server progs/comch_server.cpp)
target_link_libraries(comch_server shoc)

add_executable(comch_server_singleshot progs/comch_server_singleshot.cpp)
target_link_libraries(comch_server_singleshot shoc)

add_executable(comch_data_client progs/comch_data_client.cpp)
target_link_libraries(comch_data_client shoc)

add_executable(comch_data_server progs/comch_data_server.cpp)
target_link_libraries(comch_data_server shoc)

add_executable(simple_compress progs/simple_compress.cpp)
target_link_libraries(simple_compress shoc)

add_executable(parallel_compress progs/parallel_compress.cpp)
target_link_libraries(parallel_compress shoc)

add_executable(dma_client progs/dma_client.cpp)
target_link_libraries(dma_client shoc)

add_executable(dma_server progs/dma_server.cpp)
target_link_libraries(dma_server shoc)

add_executable(rdma_dpu_receive progs/rdma_dpu_receive.cpp)
target_link_libraries(rdma_dpu_receive shoc)

add_executable(rdma_host_send progs/rdma_host_send.cpp)
target_link_libraries(rdma_host_send shoc)

add_executable(sync_event_local_pci progs/sync_event_local_pci.cpp)
target_link_libraries(sync_event_local_pci shoc)

add_executable(sync_event_remote_pci progs/sync_event_remote_pci.cpp)
target_link_libraries(sync_event_remote_pci shoc)

add_executable(generate_testdata tools/generate_testdata.cpp)

add_executable(test-shoc
    #tests/coro/group_task.cpp
    #tests/coro/group_value_awaitable.cpp
    # keep commented until we have a crypto-enabled bluefield
    # 'tests/group_aes_gcm.cpp',
    tests/group_compress.cpp
    tests/group_dma.cpp
    tests/group_engine.cpp
    tests/group_event_sources.cpp)
target_link_libraries(test-shoc shoc GTest::gtest GTest::gtest_main)

add_executable(doca_simple_compress progs/plain_doca/simple_compress.c)
target_link_libraries(doca_simple_compress ${DOCA_LIBRARIES})

add_executable(doca_parallel_compress progs/plain_doca/parallel_compress.c)
target_link_libraries(doca_parallel_compress ${DOCA_LIBRARIES})

add_executable(doca_comch_server progs/plain_doca/comch_server.c)
target_link_libraries(doca_comch_server ${DOCA_LIBRARIES})

add_executable(doca_comch_client progs/plain_doca/comch_client.c)
target_link_libraries(doca_comch_client ${DOCA_LIBRARIES})

add_executable(doca_comch_data_server
    progs/plain_doca/comch_data_server/connection.c
    progs/plain_doca/comch_data_server/main.c
    progs/plain_doca/comch_data_server/memory.c
    progs/plain_doca/comch_data_server/server.c
)
target_link_libraries(doca_comch_data_server ${DOCA_LIBRARIES})

add_executable(doca_comch_data_client
    progs/plain_doca/comch_data_client/client.c
    progs/plain_doca/comch_data_client/consumer.c
    progs/plain_doca/comch_data_client/main.c
    progs/plain_doca/comch_data_client/memory.c
)
target_link_libraries(doca_comch_data_client ${DOCA_LIBRARIES})

add_executable(doca_dma_server
    progs/plain_doca/dma_server/main.c
    progs/plain_doca/dma_server/memory.c
    progs/plain_doca/dma_server/server.c
)
target_link_libraries(doca_dma_server ${DOCA_LIBRARIES})

add_executable(doca_dma_client
    progs/plain_doca/dma_client/comch.c
    progs/plain_doca/dma_client/common.c
    progs/plain_doca/dma_client/dma.c
    progs/plain_doca/dma_client/main.c
)
target_link_libraries(doca_dma_client ${DOCA_LIBRARIES})

add_executable(bug-comch-consumer-start-lost-server bug-reports/comch-consumer-start-lost/server.c)
target_link_libraries(bug-comch-consumer-start-lost-server ${DOCA_LIBRARIES})

add_executable(bug-comch-consumer-start-lost-client bug-reports/comch-consumer-start-lost/client.c)
target_link_libraries(bug-comch-consumer-start-lost-client ${DOCA_LIBRARIES})
