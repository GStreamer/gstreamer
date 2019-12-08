typefindfunctions_extra_defs = []
typefindfunctions_extra_deps = []
if not get_option('gio-typefinder').disabled()
  typefindfunctions_extra_defs += ['-DHAVE_GIO']
  typefindfunctions_extra_deps += [gio_dep]
endif

gsttypefind = library('gsttypefindfunctions',
  'gsttypefindfunctions.c',
  c_args : gst_plugins_base_args + typefindfunctions_extra_defs,
  include_directories: [configinc, libsinc],
  dependencies : [pbutils_dep, gst_base_dep, typefindfunctions_extra_deps],
  install : true,
  install_dir : plugins_install_dir,
)
pkgconfig.generate(gsttypefind, install_dir : plugins_pkgconfig_install_dir)
plugins += [gsttypefind]
