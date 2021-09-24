if gtk_dep.found()
  executable('giosrc-mounting', 'giosrc-mounting.c',
    c_args : gst_plugins_base_args,
    include_directories: [configinc, libsinc],
    dependencies : [glib_deps, gio_dep, gst_dep, gtk_dep],
    install: false)
endif
