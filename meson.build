project(
	'wstroke',
	'c',
	'cpp',
	'vala',
	version: '2.6.0',
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

# dependencies for loadable plugin
boost    = dependency('boost', modules: ['serialization'], static: false)
wayfire  = dependency('wayfire', version: '>=0.11.0')
wlroots  = dependency('wlroots-0.20')
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

if get_option('wf_plugin')
	# prefix will be ~/.local/share/wayfire/plugin-manager/install/

	# things that need to go under the wf-plugin prefix
	lib_dir = prefix / get_option('libdir') / 'wayfire'
	metadata_dir = prefix / 'share' / 'wayfire' / 'metadata'
	plugin_icon_dir = prefix / 'share' / 'wayfire' / 'icons'
	
	# components for the config binary -- should go under ~/.local
	tmp1 = prefix.split('/')
	if tmp1.length() < 3
		error('Cannot parse wayfire-plugin prefix!')
	endif
	if tmp1[1] != 'home'
		error('wayfire-plugin prefix does not seem to be under your home directory')
	endif
	bin_prefix = join_paths('/', tmp1[1], tmp1[2], '.local')
	datadir = bin_prefix / get_option('datadir') # ~/.local/share
	bindir = bin_prefix / get_option('bindir') # ~/.local/bin
	icon_dir = join_paths(datadir, 'icons')
	desktop_dir = join_paths(datadir, 'applications')
else
	lib_dir = wayfire.get_variable(pkgconfig: 'plugindir')
	metadata_dir = wayfire.get_variable(pkgconfig: 'metadatadir')
	plugin_icon_dir = wayfire.get_variable(pkgconfig: 'icondir')
	
	datadir = join_paths(prefix, get_option('datadir'))
	bindir = prefix / get_option('bindir') # ~/.local/bin
	icon_dir = join_paths(datadir, 'icons')
	desktop_dir = join_paths(datadir, 'applications')
endif


subdir('toplevel-grabber')
subdir('src')
subdir('example')

conf_data_desktop = configuration_data()
conf_data_desktop.set('BINDIR', bindir)
configure_file(input: 'wstroke-config.desktop.in',
               output: 'wstroke-config.desktop',
               install: true,
               install_dir: desktop_dir,
               configuration: conf_data_desktop)

install_data('wstroke.xml', install_dir: metadata_dir)
install_man('wstroke-config.1') # might go under wf-plugins prefix, but we don't care since it is a stub anyway
subdir('icons')
