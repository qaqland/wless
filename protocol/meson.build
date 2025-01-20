wayland_scanner_dep = dependency('wayland-scanner', native: true)
wayland_scanner = find_program(
    wayland_scanner_dep.get_variable('wayland_scanner'),
    native: true,
)

wayland_protocols_dep = dependency('wayland-protocols')
wayland_protocols_dir = wayland_protocols_dep.get_variable('pkgdatadir')

protocols = {'xdg-shell': 'stable/xdg-shell/xdg-shell.xml'}

foreach name, path : protocols
    private_code = custom_target(
        name.underscorify() + '_c',
        input: wayland_protocols_dir / path,
        output: '@BASENAME@-protocol.c',
        command: [wayland_scanner, 'private-code', '@INPUT@', '@OUTPUT@'],
    )
    server_header = custom_target(
        name.underscorify() + '_server_h',
        input: wayland_protocols_dir / path,
        output: '@BASENAME@-protocol.h',
        command: [wayland_scanner, 'server-header', '@INPUT@', '@OUTPUT@'],
    )
    sources += [private_code, server_header]
endforeach

