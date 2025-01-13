playback_sources = [
  'gstdecodebin2.c',
  'gstdecodebin3.c',
  'gsturidecodebin.c',
  'gsturidecodebin3.c',
  'gsturisourcebin.c',
  'gstparsebin.c',
  'gstplaybackplugin.c',
  'gstplaybackelement.c',
  'gstplaybackutils.c',
  'gstplaybin2.c',
  'gstplaybin3.c',
  'gstplaysink.c',
  'gstplay-enum.c',
  'gstsubtitleoverlay.c',
  'gstplaysinkvideoconvert.c',
  'gstplaysinkaudioconvert.c',
  'gstplaysinkconvertbin.c',
  'gststreamsynchronizer.c'
]

playback_headers = [
  'gstplaysinkaudioconvert.h',
  'gstplaybackelements.h',
  'gstplaysinkvideoconvert.h',
  'gstplaysink.h',
  'gstrawcaps.h',
  'gstplaybackutils.h',
  'gstplay-enum.h',
  'gststreamsynchronizer.h',
  'gstplaysinkconvertbin.h',
  'gstsubtitleoverlay.h',
]

doc_sources = []
foreach s: playback_sources + playback_headers
  doc_sources += meson.current_source_dir() / s
endforeach

plugin_sources += {
  'playback': pathsep.join(doc_sources)
}

if get_option('playback').disabled()
  subdir_done()
endif

gstplayback = library('gstplayback',
  playback_sources,
  c_args : gst_plugins_base_args + ['-DCOLORSPACE="videoconvert"'],
  include_directories: [configinc, libsinc],
  dependencies : [audio_dep, video_dep, pbutils_dep, tag_dep],
  install : true,
  install_dir : plugins_install_dir,
)
plugins += [gstplayback]
