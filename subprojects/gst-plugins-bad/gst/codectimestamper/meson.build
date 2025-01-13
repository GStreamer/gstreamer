codectimestamper_sources = [
  'gstcodectimestamper.c',
  'gsth264timestamper.c',
  'gsth265timestamper.c',
  'plugin.c',
]

codectimestamper_headers = [
  'gstcodectimestamper.h',
  'gsth265timestamper.h',
  'gsth264timestamper.h',
]

doc_sources = []
foreach s: codectimestamper_sources + codectimestamper_headers
  doc_sources += meson.current_source_dir() / s
endforeach

plugin_sources += {
  'codectimestamper': pathsep.join(doc_sources)
}

if get_option('codectimestamper').disabled()
  subdir_done()
endif

gstcodectimestamper = library('gstcodectimestamper',
  codectimestamper_sources,
  c_args : gst_plugins_bad_args + [ '-DGST_USE_UNSTABLE_API' ],
  include_directories : [configinc],
  dependencies : [gstcodecparsers_dep, gstbase_dep, gstvideo_dep],
  install : true,
  install_dir : plugins_install_dir,
)
plugins += [gstcodectimestamper]
