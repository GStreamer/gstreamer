inter_sources = [
  'gstinteraudiosink.c',
  'gstinteraudiosrc.c',
  'gstintersubsink.c',
  'gstintersubsrc.c',
  'gstintervideosink.c',
  'gstintervideosrc.c',
  'gstinter.c',
  'gstintersurface.c',
]

inter_headers = [
  'gstinteraudiosink.h',
  'gstintersurface.h',
  'gstintervideosrc.h',
  'gstintersubsink.h',
  'gstintervideosink.h',
  'gstinteraudiosrc.h',
  'gstintersubsrc.h',
]

doc_sources = []
foreach s: inter_sources + inter_headers
  doc_sources += meson.current_source_dir() / s
endforeach

plugin_sources += {
  'inter': pathsep.join(doc_sources)
}

if get_option('inter').disabled()
  subdir_done()
endif

gstinter = library('gstinter',
  inter_sources,
  c_args : gst_plugins_bad_args,
  include_directories : [configinc],
  dependencies : [gstaudio_dep, gstvideo_dep, gstbase_dep],
  install : true,
  install_dir : plugins_install_dir,
)
plugins += [gstinter]
