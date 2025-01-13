vfilter_sources = [
  'plugin.c',
  'gstvideoflip.c',
  'gstvideobalance.c',
  'gstgamma.c',
  'gstvideomedian.c',
]

vfilter_headers = [
  'gstvideomedian.h',
  'gstvideoflip.h',
  'gstvideobalance.h',
  'gstgamma.h',
]

doc_sources = []
foreach s: vfilter_sources + vfilter_headers
  doc_sources += meson.current_source_dir() / s
endforeach

plugin_sources += {
  'videofilter': pathsep.join(doc_sources)
}

if get_option('videofilter').disabled()
  subdir_done()
endif

gstvideofilter = library('gstvideofilter',
  vfilter_sources,
  c_args : gst_plugins_good_args,
  include_directories : [configinc],
  dependencies : [gstbase_dep, gstvideo_dep, libm],
  install : true,
  install_dir : plugins_install_dir,
)
plugins += [gstvideofilter]
