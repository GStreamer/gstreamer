if static_build
  warning('Validate plugins are disabled in static build mode')
  subdir_done()
endif

subdir('fault_injection')
subdir('gapplication')
subdir('ssim')

if gtk_dep.found()
    subdir('gtk')
endif

meson.add_devenv({'GST_VALIDATE_PLUGIN_PATH' : meson.current_build_dir()}, method: 'prepend')
