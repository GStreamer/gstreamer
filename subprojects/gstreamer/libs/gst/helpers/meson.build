subdir('ptp')

gst_helpers = {
  'gst-plugin-scanner': {
    'files': files('gst-plugin-scanner.c'),
    'deps': [gst_dep, mathlib, gmodule_dep],
    'env': ['GST_PLUGIN_SCANNER', 'exe-full-path'],
    'install_dir': helpers_install_dir,
  },
  'gst-completion-helper': {
    'files': files('gst-completion-helper.c'),
    'deps': [gst_dep],
    'install_dir': helpers_install_dir,
  },
}

if not get_option('tools').disabled() and not static_build
  foreach helper, data: gst_helpers
    exe_name = '@0@'.format(helper)
    exe = executable(exe_name,
      data.get('files'),
      install: data.get('install', true),
      install_tag: 'bin',
      install_dir:  data.get('install_dir', get_option('bindir')),
      include_directories : [configinc],
      dependencies : data.get('deps', []),
      c_args: data.get('extra_c_args', []) + ['-DG_LOG_DOMAIN="@0@"'.format(exe_name)],
    )
    if(data.has_key('env'))
        env = data.get(('env'))
        value = env[1]
        if env[1] == 'exe-full-path'
          value = exe.full_path()
        endif
        meson.add_devenv({env[0]: value})
    endif

    if data.has_key('man_page')
      install_man(data.get('man_page'))
    endif
  endforeach
endif

# Used in test env setup to make tests find plugin scanner in build tree
gst_scanner_dir = meson.current_build_dir()


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
