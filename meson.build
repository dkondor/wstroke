project(
	'wstroke',
	'c',
	'cpp',
	'vala',
	version: '2.3.0',
	license: 'MIT',
	meson_version: '>=1.0.0',
	default_options: [
		'cpp_std=c++17',
        'c_std=c11',
		'warning_level=2',
		'werror=false',
	],
)

# paths (only needed to install icon and desktop file)
prefix = get_option('prefix')
datadir = join_paths(prefix, get_option('datadir'))
icon_dir = join_paths(datadir, 'icons')
desktop_dir = join_paths(datadir, 'applications')

# dependencies for loadable plugin
boost    = dependency('boost', modules: ['serialization'], static: false)
wayfire  = dependency('wayfire', version: '>=0.10.0')
wlroots  = dependency('wlroots-0.18')
wlroots_headers = wlroots.partial_dependency(includes: true, compile_args: true)
wlserver = dependency('wayland-server')
glibmm   = dependency('glibmm-2.4')
cairo    = dependency('cairo')
pixman   = dependency('pixman-1')

# additional dependencies for GUI
gtkmm    = dependency('gtkmm-3.0')
gdkmm    = dependency('gdkmm-3.0')
glib     = dependency('glib-2.0')
gobject  = dependency('gobject-2.0')
gtk      = dependency('gtk+-3.0')
gdk      = dependency('gdk-3.0')

gnome = import('gnome')

# filesystem library support
# note: on Ubuntu 18.04 this only works with clang++
cpp = meson.get_compiler('cpp')
if cpp.has_link_argument('-lc++fs')
  add_project_link_arguments(['-lc++fs'], language: 'cpp')
elif cpp.has_link_argument('-lc++experimental')
  add_project_link_arguments(['-lc++experimental'], language: 'cpp')
elif cpp.has_link_argument('-lstdc++fs')
  add_project_link_arguments(['-lstdc++fs'], language: 'cpp')
endif


# wayland-scanner -- needed for keyboard grabber and input inhibitor
wayland_client = dependency('wayland-client')
wayland_scanner = find_program('wayland-scanner')

wayland_scanner_code = generator(
	wayland_scanner,
	output: '@BASENAME@-protocol.c',
	arguments: ['private-code', '@INPUT@', '@OUTPUT@'],
)

wayland_scanner_client = generator(
	wayland_scanner,
	output: '@BASENAME@-client-protocol.h',
	arguments: ['client-header', '@INPUT@', '@OUTPUT@'],
)


add_project_arguments(['--vapidir', meson.current_source_dir() + '/src'], language: 'vala')
add_project_arguments(['--pkg', 'input_inhibitor'], language: 'vala')

subdir('input-inhibitor')
subdir('toplevel-grabber')
subdir('src')
subdir('example')


install_data('wstroke.xml', install_dir: wayfire.get_variable(pkgconfig: 'metadatadir'))
install_data('wstroke-config.desktop', install_dir: desktop_dir)
install_man('wstroke-config.1')
subdir('icons')
