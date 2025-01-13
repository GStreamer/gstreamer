audiovis_sources = [
  'plugin.c',
  'gstspacescope.c',
  'gstspectrascope.c',
  'gstsynaescope.c',
  'gstwavescope.c',
]

audiovis_headers = [
  'gstwavescope.h',
  'gstsynaescope.h',
  'gstdrawhelpers.h',
  'gstspectrascope.h',
  'gstspacescope.h',
]

doc_sources = []
foreach s: audiovis_sources + audiovis_headers
  doc_sources += meson.current_source_dir() / s
endforeach

plugin_sources += {
  'audiovisualizers': pathsep.join(doc_sources)
}

if get_option('audiovisualizers').disabled()
  subdir_done()
endif

gstaudiovisualizers = library('gstaudiovisualizers',
  audiovis_sources,
  c_args : gst_plugins_bad_args,
  include_directories : [configinc],
  dependencies : [gstbase_dep, gstpbutils_dep, gstaudio_dep, gstvideo_dep, gstfft_dep, libm],
  install : true,
  install_dir : plugins_install_dir,
)
plugins += [gstaudiovisualizers]
