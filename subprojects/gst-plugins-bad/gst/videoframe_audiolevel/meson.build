videoframe_audiolevel_sources = [
  'gstvideoframe-audiolevel.c',
]

videoframe_audiolevel_headers = [
  'gstvideoframe-audiolevel.h',
]

doc_sources = []
foreach s: videoframe_audiolevel_sources + videoframe_audiolevel_headers
  doc_sources += meson.current_source_dir() / s
endforeach

plugin_sources += {
  'videoframe_audiolevel': pathsep.join(doc_sources)
}

if get_option('videoframe_audiolevel').disabled()
  subdir_done()
endif

gstvideoframe_audiolevel = library('gstvideoframe_audiolevel',
  videoframe_audiolevel_sources,
  c_args : gst_plugins_bad_args,
  include_directories : [configinc],
  dependencies : [gstvideo_dep, gstaudio_dep, libm],
  install : true,
  install_dir : plugins_install_dir,
)
plugins += [gstvideoframe_audiolevel]
