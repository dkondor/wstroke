client_protocols = [
    './wlr-input-inhibitor-unstable-v1.xml'
]

wl_protos_client_src = []
wl_protos_headers = []

foreach p : client_protocols
	xml = join_paths(p)
	wl_protos_headers += wayland_scanner_client.process(xml)
	wl_protos_client_src += wayland_scanner_code.process(xml)
endforeach

lib_inhibitor_protos = static_library('wl_inhibitor_protos', wl_protos_client_src + wl_protos_headers,
	dependencies: [wayland_client]) # for the include directory

protos = declare_dependency(
	link_with: lib_inhibitor_protos,
	sources: wl_protos_headers,
)

input_inhibitor = static_library('input_inhibitor', 'input_inhibitor.c',
	dependencies: [wayland_client, protos, gdk])

input_inhibitor_dep = declare_dependency(
	link_with: input_inhibitor,
	include_directories: include_directories('.')
)
