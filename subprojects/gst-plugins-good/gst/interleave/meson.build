interleave_sources = [
  'plugin.c',
  'interleave.c',
  'deinterleave.c',
]

interleave_headers = [
  'deinterleave.h',
  'gstinterleaveelements.h',
  'interleave.h',
]

doc_sources = []
foreach s: interleave_sources + interleave_headers
  doc_sources += meson.current_source_dir() / s
endforeach

plugin_sources += {
  'interleave': pathsep.join(doc_sources)
}

if get_option('interleave').disabled()
  subdir_done()
endif

gstinterleave = library('gstinterleave',
  interleave_sources,
  c_args : gst_plugins_good_args,
  include_directories : [configinc, libsinc],
  dependencies : [gstbase_dep, gstaudio_dep],
  install : true,
  install_dir : plugins_install_dir,
)
plugins += [gstinterleave]
