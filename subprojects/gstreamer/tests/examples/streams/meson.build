executable('stream-status', 'stream-status.c',
  install: false,
  dependencies : [glib_dep, gobject_dep, gmodule_dep, mathlib, gst_dep],
  c_args: gst_c_args,
)

if cc.has_header('pthread.h')
  threads_dep = dependency('threads')
  executable('rtpool-test', 'rtpool-test.c', 'testrtpool.c',
    install: false,
    dependencies : [glib_dep, gobject_dep, gmodule_dep, mathlib, gst_dep, threads_dep],
    c_args: gst_c_args,
  )
endif
