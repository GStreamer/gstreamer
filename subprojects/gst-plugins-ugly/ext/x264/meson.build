x264_opt = get_option('x264').require(gpl_allowed, error_message: '''
  Plugin x264 explicitly required via options but GPL-licensed plugins disabled via options.
  Pass option -Dgpl=enabled to Meson to allow GPL-licensed plugins to be built.
  ''')

x264_sources = [
  'gstx264enc.c',
  'gstencoderbitrateprofilemanager.c',
]

x264_dep = dependency('x264', version : '>=0.156', required : x264_opt,
                      fallback: ['x264', 'libx264_dep'])

if x264_dep.found()
  x264_libraries = get_option('x264_libraries')
  x264_args = []
  if x264_libraries != ''
    x264_args += ['-DHAVE_X264_ADDITIONAL_LIBRARIES="@0@"'.format(x264_libraries)]
    extra_gmodule_dep = [gmodule_dep]
  else
    extra_gmodule_dep = []
  endif

  gstx264 = library('gstx264',
    x264_sources,
    c_args : ugly_args + x264_args,
    include_directories : [configinc],
    dependencies : [gstbase_dep, gstvideo_dep, gstpbutils_dep, x264_dep] + extra_gmodule_dep,
    install : true,
    install_dir : plugins_install_dir,
  )
  plugins += [gstx264]

  install_data(sources: 'GstX264Enc.prs', install_dir: presetdir)
  env = environment()
  env.prepend('GST_PRESET_PATH', meson.current_source_dir())
  meson.add_devenv(env)
endif
