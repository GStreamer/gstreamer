subdir('ptp')

exe = executable('gst-plugin-scanner',
  'gst-plugin-scanner.c',
  c_args : gst_c_args,
  include_directories : [configinc],
  dependencies : [gst_dep, mathlib, gmodule_dep,],
  install_dir : helpers_install_dir,
  install: true,
)

meson.add_devenv({'GST_PLUGIN_SCANNER': exe.full_path()})

# Used in test env setup to make tests find plugin scanner in build tree
gst_scanner_dir = meson.current_build_dir()

if bashcomp_found
  executable('gst-completion-helper',
    'gst-completion-helper.c',
    c_args : gst_c_args,
    include_directories : [configinc],
    dependencies : [gst_dep],
    install_dir : helpers_install_dir,
    install_tag : 'bin',
    install: true,
  )
endif

install_data(['gst_gdb.py', 'glib_gobject_helper.py'],
  install_dir : join_paths(get_option('datadir'), 'gstreamer-@0@'.format(apiversion), 'gdb'),
  install_tag : 'devel')

# This is needed to make gdb find gst_gdb.py
env = environment()
env.prepend('PYTHONPATH', meson.current_source_dir())
meson.add_devenv(env)

gdbconf = configuration_data()
gdbconf.set('GST_API_VERSION', apiversion)
gdbconf.set('DATADIR', '@0@/@1@'.format(get_option('prefix'), get_option('datadir')))

if host_system != 'windows'
  # XXX: We add a leading './' because prefix is an absolute path and we
  # need it to be a relative path so that join_paths appends it to the end.
  gdb_install_dir = join_paths(get_option('datadir'), 'gdb', 'auto-load', './' + get_option('prefix'), get_option('libdir'))
else
  # FIXME: Cannot install on Windows because the path will contain a drive
  # letter and colons are not allowed in paths.
  gdb_install_dir = disabler()
endif
configure_file(input : 'libgstreamer-gdb.py.in',
  output : 'libgstreamer-@0@.so.@1@-gdb.py'.format(apiversion, libversion),
  install_dir : gdb_install_dir,
  install_tag : 'devel',
  configuration : gdbconf)
