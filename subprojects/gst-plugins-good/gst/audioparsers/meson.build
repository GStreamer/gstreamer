audioparsers_sources = [
  'gstaacparse.c',
  'gstamrparse.c',
  'gstac3parse.c',
  'gstdcaparse.c',
  'gstflacparse.c',
  'gstmpegaudioparse.c',
  'gstsbcparse.c',
  'gstwavpackparse.c',
  'plugin.c',
]

audioparsers_headers = [
  'gstamrparse.h',
  'gstaacparse.h',
  'gstdcaparse.h',
  'gstsbcparse.h',
  'gstmpegaudioparse.h',
  'gstac3parse.h',
  'gstwavpackparse.h',
  'gstflacparse.h',
  'gstaudioparserselements.h',
]

doc_sources = []
foreach s: audioparsers_sources + audioparsers_headers
  doc_sources += meson.current_source_dir() / s
endforeach

plugin_sources += {
  'audioparsers': pathsep.join(doc_sources)
}

if get_option('audioparsers').disabled()
  subdir_done()
endif

gstaudioparsers = library('gstaudioparsers',
  audioparsers_sources,
  c_args : gst_plugins_good_args,
  include_directories : [configinc],
  dependencies : [gst_dep, gstbase_dep, gstpbutils_dep,
                  gstaudio_dep, gsttag_dep],
  install : true,
  install_dir : plugins_install_dir,
)
plugins += [gstaudioparsers]
