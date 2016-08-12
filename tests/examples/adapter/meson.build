if cdata.has('HAVE_SYS_TIMES_H') and cdata.has('HAVE_UNISTD_H')
  executable('adapter_test', 'adapter_test.c',
    install: false,
    dependencies : [glib_dep, gobject_dep, gmodule_dep, mathlib, gst_dep, gst_base_dep],
    c_args: gst_c_args,
  )
endif
