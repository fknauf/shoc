project('doca_sample', 'cpp',
    default_options: ['buildtype=debug', 'cpp_std=c++20', 'warning_level=2'],
    meson_version: '>= 0.61.2'
)

SAMPLE_NAME = 'doca-cpp-eventbased'

# Comment this line to restore warnings of experimental DOCA features
add_project_arguments('-D DOCA_ALLOW_EXPERIMENTAL_API', language: ['c', 'cpp'])
add_project_arguments('-std=c++20', language: ['cpp'])

deps = [ dependency('doca'), dependency('spdlog') ]

common_srcs = [
    'doca/buffer.cpp',
    'doca/buffer_inventory.cpp',
    'doca/buffer_pool.cpp',
    'doca/comch_client.cpp',
    'doca/comch_device.cpp',
    'doca/comch_producer.cpp',
    'doca/comch_server.cpp',
    'doca/context.cpp',
    'doca/device.cpp',
    'doca/epoll_handle.cpp',
    'doca/logger.cpp',
    'doca/memory_map.cpp',
    'doca/progress_engine.cpp'
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
