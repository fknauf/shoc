project('doca_sample', [ 'c', 'cpp' ], 
    default_options: ['buildtype=debug', 'cpp_std=c++20', 'warning_level=2'],
    meson_version: '>= 0.61.2'
)

SAMPLE_NAME = 'doca-cpp-eventbased'

# Comment this line to restore warnings of experimental DOCA features
add_project_arguments('-D DOCA_ALLOW_EXPERIMENTAL_API', language: ['c', 'cpp'])
# add_project_arguments('-std=c++20', language: ['cpp'])
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
    'doca/aes_gcm.cpp',
    'doca/buffer.cpp',
    'doca/buffer_inventory.cpp',
    'doca/buffer_pool.cpp',
    'doca/comch/client.cpp',
    'doca/comch/consumer.cpp',
    'doca/comch/producer.cpp',
    'doca/comch/server.cpp',
    'doca/compress.cpp',
    'doca/context.cpp',
    'doca/device.cpp',
    'doca/dma.cpp',
    'doca/epoll_handle.cpp',
    'doca/event_sources.cpp',
    'doca/logger.cpp',
    'doca/memory_map.cpp',
    'doca/progress_engine.cpp',
    'doca/rdma.cpp',
    'doca/sync_event.cpp'
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

executable('test-docapp',
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
