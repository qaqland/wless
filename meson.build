project(
    'wless',
    'c',
    version: '0.0.2',
    default_options: ['warning_level=3', 'c_std=c2x'],
)

add_project_arguments(
    '-D_POSIX_C_SOURCE=200809L',
    '-DWLR_USE_UNSTABLE',
    '-DWLESS_VERSION="@0@"'.format(meson.project_version()),
    language: 'c',
)

if get_option('buildtype').startswith('debug')
    add_project_arguments('-DDEBUG', language: 'c')
endif

sources = files('action.c', 'client.c', 'input.c', 'output.c', 'wless.c')

subdir('protocol')

wayland_server = dependency('wayland-server')
wlroots = dependency('wlroots-0.19')
xkbcommon = dependency('xkbcommon')

executable(
    'wless',
    sources,
    install: true,
    dependencies: [wayland_server, wlroots, xkbcommon],
)
