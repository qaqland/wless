wayland_scanner_dep = dependency('wayland-scanner', native: true)
wayland_scanner = find_program(
    wayland_scanner_dep.get_variable('wayland_scanner'),
    native: true,
)

wl_protocols_dep = dependency('wayland-protocols')
wl_protocols_dir = wl_protocols_dep.get_variable('pkgdatadir')

wlr_protocols_dep = dependency('wlr-protocols')
wlr_protocols_dir = wlr_protocols_dep.get_variable('pkgdatadir')

protocols = [
    wl_protocols_dir / 'stable/xdg-shell/xdg-shell.xml',
    wlr_protocols_dir / 'unstable/wlr-layer-shell-unstable-v1.xml',
]

foreach path : protocols
    private_code = custom_target(
        path.underscorify() + '_c',
        input: path,
        output: '@BASENAME@-protocol.c',
        command: [wayland_scanner, 'private-code', '@INPUT@', '@OUTPUT@'],
    )
    server_header = custom_target(
        path.underscorify() + '_server_h',
        input: path,
        output: '@BASENAME@-protocol.h',
        command: [wayland_scanner, 'server-header', '@INPUT@', '@OUTPUT@'],
    )
    sources += [private_code, server_header]
endforeach

