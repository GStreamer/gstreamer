if not use_wayland
  subdir_done()
endif

if gtk_dep.found() and gtk_wayland_dep.found()
  executable('gtkwaylandsink',
    'gtkwaylandsink.c',
    extra_files: ['window.ui'],
    install: false,
    include_directories : [configinc],
    dependencies : [gtk_dep, gst_dep],
    c_args : gst_plugins_bad_args,
  )
endif
