amrnb_dep = dependency('opencore-amrnb', version : '>= 0.1.3', required : get_option('amrnb'))

amrnb_sources = [
  'amrnb.c',
  'amrnbdec.c',
  'amrnbenc.c',
]

amrnb_headers = [
  'amrnbdec.h',
  'amrnbenc.h',
]

doc_sources = []
foreach s: amrnb_sources + amrnb_headers
  doc_sources += meson.current_source_dir() / s
endforeach

plugin_sources += {
  'amrnb': pathsep.join(doc_sources)
}

if amrnb_dep.found()
  amrnb = library('gstamrnb',
    amrnb_sources,
    c_args : gst_plugins_good_args,
    include_directories : [configinc],
    dependencies : [gstaudio_dep, amrnb_dep],
    install : true,
    install_dir : plugins_install_dir,
  )
  plugins += [amrnb]

  install_data(sources: 'GstAmrnbEnc.prs', install_dir: presetdir)

  env = environment()
  env.prepend('GST_PRESET_PATH', meson.current_source_dir())
  meson.add_devenv(env)
endif
