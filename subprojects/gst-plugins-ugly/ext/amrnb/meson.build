amrnb_dep = dependency('opencore-amrnb', version : '>= 0.1.3', required : get_option('amrnb'))

if amrnb_dep.found()
  amrnb = library('gstamrnb',
    ['amrnb.c', 'amrnbdec.c', 'amrnbenc.c'],
    c_args : ugly_args,
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
