project('doca_sample', [ 'c', 'cpp' ],
    default_options: ['buildtype=debug', 'cpp_std=c++20', 'c_std=c18', 'warning_level=2'],
    meson_version: '>= 0.61.2'
)

SAMPLE_NAME = 'doca-cpp-eventbased'

add_project_arguments('-D DOCA_ALLOW_EXPERIMENTAL_API', language: ['c', 'cpp'])
# for clock_gettime in plain-doca benchmarks
add_project_arguments('-D _POSIX_C_SOURCE=200809L', language: ['c'])
add_project_arguments('-fcoroutines', language: ['cpp'])

deps = [
    dependency('doca-aes-gcm'),
    dependency('doca-common'),
    dependency('doca-compress'),
    dependency('doca-comch'),
    dependency('doca-dma'),
    dependency('doca-rdma'),
    dependency('spdlog')
]

common_srcs = [
    'shoc/aes_gcm.cpp',
    'shoc/buffer.cpp',
    'shoc/buffer_inventory.cpp',
    'shoc/buffer_pool.cpp',
    'shoc/comch/client.cpp',
    'shoc/comch/consumer.cpp',
    'shoc/comch/producer.cpp',
    'shoc/comch/server.cpp',
    'shoc/compress.cpp',
    'shoc/context.cpp',
    'shoc/device.cpp',
    'shoc/dma.cpp',
    'shoc/epoll_handle.cpp',
    'shoc/event_sources.cpp',
    'shoc/logger.cpp',
    'shoc/memory_map.cpp',
    'shoc/progress_engine.cpp',
    'shoc/rdma.cpp',
    'shoc/sync_event.cpp'
]

inc_dirs  = [
    '/opt/mellanox/doca/include'
]

libdocapp = static_library('doca-cpp-eventbased', common_srcs,
        include_directories: inc_dirs,
        install: false)

executable('comch_client', [ 'progs/comch_client.cpp' ],
        dependencies : deps,
        link_with: libdocapp,
        include_directories: inc_dirs,
        install: false)

executable('comch_server', [ 'progs/comch_server.cpp' ],
        dependencies : deps,
        link_with: libdocapp,
        include_directories: inc_dirs,
        install: false)

executable('comch_server_singleshot', [ 'progs/comch_server_singleshot.cpp' ],
        dependencies : deps,
        link_with: libdocapp,
        include_directories: inc_dirs,
        install: false)

executable('comch_data_client', [ 'progs/comch_data_client.cpp' ],
        dependencies : deps,
        link_with: libdocapp,
        include_directories: inc_dirs,
        install: false)

executable('comch_data_server', [ 'progs/comch_data_server.cpp' ],
        dependencies : deps,
        link_with: libdocapp,
        include_directories: inc_dirs,
        install: false)

executable('simple_compress', [ 'progs/simple_compress.cpp' ],
        dependencies : deps,
        link_with: libdocapp,
        include_directories: inc_dirs,
        install: false)

executable('parallel_compress', [ 'progs/parallel_compress.cpp' ],
        dependencies : deps,
        link_with: libdocapp,
        include_directories: inc_dirs,
        install: false)

executable('dma_client', [ 'progs/dma_client.cpp' ],
        dependencies : deps,
        link_with: libdocapp,
        include_directories: inc_dirs,
        install: false)

executable('dma_server', [ 'progs/dma_server.cpp' ],
        dependencies : deps,
        link_with: libdocapp,
        include_directories: inc_dirs,
        install: false)

executable('rdma_dpu_receive', [ 'progs/rdma_dpu_receive.cpp' ],
        dependencies : deps,
        link_with: libdocapp,
        include_directories: inc_dirs,
        install: false)

executable('rdma_host_send', [ 'progs/rdma_host_send.cpp' ],
        dependencies : deps,
        link_with: libdocapp,
        include_directories: inc_dirs,
        install: false)

executable('sync_event_local_pci', [ 'progs/sync_event_local_pci.cpp' ],
        dependencies : deps,
        link_with: libdocapp,
        include_directories: inc_dirs,
        install: false)

executable('sync_event_remote_pci', [ 'progs/sync_event_remote_pci.cpp' ],
        dependencies : deps,
        link_with: libdocapp,
        include_directories: inc_dirs,
        install: false)

executable('generate_testdata', [ 'tools/generate_testdata.cpp' ],
        install: false)

executable('test-shoc',
        [
                'tests/coro/group_task.cpp',
                'tests/coro/group_value_awaitable.cpp',
                # keep commented until we have a crypto-enabled bluefield
                # 'tests/group_aes_gcm.cpp',
                'tests/group_compress.cpp',
                'tests/group_dma.cpp',
                'tests/group_engine.cpp',
                'tests/group_event_sources.cpp'
        ],
        dependencies : deps + [ dependency('gtest', main: true) ],
        link_with: libdocapp,
        include_directories: inc_dirs,
        install: false)

executable('doca_simple_compress', [ 'progs/plain_doca/simple_compress.c' ],
        dependencies : deps,
        install: false)

executable('doca_parallel_compress', [ 'progs/plain_doca/parallel_compress.c' ],
        dependencies : deps,
        install: false)

executable('doca_comch_server', [ 'progs/plain_doca/comch_server.c' ],
        dependencies : deps,
        install: false)

executable('doca_comch_client', [ 'progs/plain_doca/comch_client.c' ],
        dependencies : deps,
        install: false)

executable('doca_comch_data_server',
        [
                'progs/plain_doca/comch_data_server/connection.c',
                'progs/plain_doca/comch_data_server/main.c',
                'progs/plain_doca/comch_data_server/memory.c',
                'progs/plain_doca/comch_data_server/server.c'
        ],
        dependencies : deps,
        install: false
)

executable('doca_comch_data_client',
        [
                'progs/plain_doca/comch_data_client/client.c',
                'progs/plain_doca/comch_data_client/consumer.c',
                'progs/plain_doca/comch_data_client/main.c',
                'progs/plain_doca/comch_data_client/memory.c'
        ],
        dependencies : deps,
        install: false
)

executable('doca_dma_server',
        [
                'progs/plain_doca/dma_server/main.c',
                'progs/plain_doca/dma_server/memory.c',
                'progs/plain_doca/dma_server/server.c'
        ],
        dependencies : deps,
        install: false
)

executable('doca_dma_client',
        [
                'progs/plain_doca/dma_client/comch.c',
                'progs/plain_doca/dma_client/common.c',
                'progs/plain_doca/dma_client/dma.c',
                'progs/plain_doca/dma_client/main.c'
        ],
        dependencies : deps,
        install: false
)

executable('bug-comch-consumer-start-lost-server', [ 'bug-reports/comch-consumer-start-lost/server.c' ], dependencies: deps, install: false)
executable('bug-comch-consumer-start-lost-client', [ 'bug-reports/comch-consumer-start-lost/client.c' ], dependencies: deps, install: false)
