# toplevel-grabber library and example program
grabber_protocols = [
    './wlr-foreign-toplevel-management-unstable-v1.xml'
]

grabber_protos_client_src = []
grabber_protos_headers = []

foreach p : grabber_protocols
	xml = join_paths(p)
	grabber_protos_headers += wayland_scanner_client.process(xml)
	grabber_protos_client_src += wayland_scanner_code.process(xml)
endforeach

lib_grabber_protos = static_library('grabber_protos', grabber_protos_client_src + grabber_protos_headers,
	dependencies: [wayland_client]) # for the include directory

lib_grabber_protos_dep = declare_dependency(
	link_with: lib_grabber_protos,
	sources: grabber_protos_headers,
)

toplevel_grabber = static_library('toplevel_grabber', 'toplevel-grabber.c',
	dependencies: [wayland_client, lib_grabber_protos_dep],
	c_args: ['-D_POSIX_C_SOURCE=200809L'])

toplevel_grabber_dep = declare_dependency(
	link_with: toplevel_grabber,
	include_directories: include_directories('.')
)

toplevel_grabber_test = executable('tl_grabber_test', 'toplevel-grabber-test.c',
	dependencies: [toplevel_grabber_dep],
	install: false)
