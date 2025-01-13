replaygain_sources = [
  'gstrganalysis.c',
  'gstrglimiter.c',
  'gstrgvolume.c',
  'replaygain.c',
  'rganalysis.c',
]

replaygain_headers = [
  'replaygain.h',
  'gstrglimiter.h',
  'rganalysis.h',
  'gstrgvolume.h',
  'gstrganalysis.h',
]

doc_sources = []
foreach s: replaygain_sources + replaygain_headers
  doc_sources += meson.current_source_dir() / s
endforeach

plugin_sources += {
  'replaygain': pathsep.join(doc_sources)
}

if get_option('replaygain').disabled()
  subdir_done()
endif

gstreplaygain = library('gstreplaygain',
  replaygain_sources,
  c_args : gst_plugins_good_args,
  include_directories : [configinc],
  dependencies : [gst_dep, gstbase_dep, gstpbutils_dep, gstaudio_dep, libm],
  install : true,
  install_dir : plugins_install_dir,
)
plugins += [gstreplaygain]
