audiotest_sources = [
  'gstaudiotestsrc.c'
]

audiotest_headers = [
  'gstaudiotestsrc.h',
]

doc_sources = []
foreach s: audiotest_sources + audiotest_headers
  doc_sources += meson.current_source_dir() / s
endforeach

plugin_sources += {
  'audiotestsrc': pathsep.join(doc_sources)
}

if get_option('audiotestsrc').disabled()
  subdir_done()
endif

audiotest = library('gstaudiotestsrc',
  audiotest_sources + [gstaudio_h],
  c_args: gst_plugins_base_args,
  include_directories: [configinc, libsinc],
  dependencies : [audio_dep, gst_base_dep, libm],
  install : true,
  install_dir : plugins_install_dir,
)
plugins += [audiotest]
