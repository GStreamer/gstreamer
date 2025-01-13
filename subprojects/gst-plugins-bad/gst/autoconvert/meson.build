autocon_sources = [
  'gstbaseautoconvert.c',
  'gstautoconvert.c',
  'gstautovideo.c',
  'gstautovideoconvert.c',
  'gstautodeinterlace.c',
  'gstautovideoflip.c',
  'plugin.c',
]

autocon_headers = [
  'gstautovideoconvert.h',
  'gstbaseautoconvert.h',
  'gstautoconvert.h',
  'gstautovideoflip.h',
  'gstautodeinterlace.h',
  'gstautovideo.h',
]

doc_sources = []
foreach s: autocon_sources + autocon_headers
  doc_sources += meson.current_source_dir() / s
endforeach

plugin_sources += {
  'autoconvert': pathsep.join(doc_sources)
}

if get_option('autoconvert').disabled()
  subdir_done()
endif

gstautoconvert = library('gstautoconvert',
  autocon_sources,
  c_args : gst_plugins_bad_args,
  include_directories : [configinc],
  dependencies : [gstbase_dep, gstpbutils_dep, gstvideo_dep],
  install : true,
  install_dir : plugins_install_dir,
)
plugins += [gstautoconvert]
