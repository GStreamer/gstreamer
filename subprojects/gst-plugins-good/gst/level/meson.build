level_sources = [
  'gstlevel.c',
]

level_headers = [
  'gstlevel.h',
]

doc_sources = []
foreach s: level_sources + level_headers
  doc_sources += meson.current_source_dir() / s
endforeach

plugin_sources += {
  'level': pathsep.join(doc_sources)
}

if get_option('level').disabled()
  subdir_done()
endif

gstlevel = library('gstlevel',
  level_sources,
  c_args : gst_plugins_good_args,
  include_directories : [configinc],
  dependencies : [gstbase_dep, gstaudio_dep, libm],
  install : true,
  install_dir : plugins_install_dir,
)
plugins += [gstlevel]
