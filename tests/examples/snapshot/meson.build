if gtk_dep.found()
  executable('snapshot', 'snapshot.c',
    c_args : gst_plugins_base_args,
    include_directories: [configinc, libsinc],
    dependencies : [libm, glib_deps, gst_dep, video_dep, gtk_dep],
    install: false)
endif
