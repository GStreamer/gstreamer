typefindfunctions_extra_defs = []
typefindfunctions_extra_deps = []
if not get_option('gio-typefinder').disabled()
  typefindfunctions_extra_defs += ['-DHAVE_GIO']
  typefindfunctions_extra_deps += [gio_dep]
endif

typefindfunctions_sources = [
'gsttypefindfunctions.c',
'gsttypefindfunctionsdata.c',
'gsttypefindfunctionsplugin.c',
'gsttypefindfunctionsriff.c',
'gsttypefindfunctionsstartwith.c',
]

typefindfunctions_headers = [
  'gsttypefindfunctionsdata.h',
  'gsttypefindfunctionsplugin.h',
]

doc_sources = []
foreach s: typefindfunctions_sources + typefindfunctions_headers
  doc_sources += meson.current_source_dir() / s
endforeach

plugin_sources += {
  'typefindfunctions': pathsep.join(doc_sources)
}

if get_option('typefind').disabled()
  subdir_done()
endif

gsttypefind = library('gsttypefindfunctions',
  typefindfunctions_sources,
  c_args : gst_plugins_base_args + typefindfunctions_extra_defs,
  include_directories: [configinc, libsinc],
  dependencies : [pbutils_dep, gst_base_dep, typefindfunctions_extra_deps],
  install : true,
  install_dir : plugins_install_dir,
)
plugins += [gsttypefind]
