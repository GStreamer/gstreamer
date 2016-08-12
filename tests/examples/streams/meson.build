executable('stream-status', 'stream-status.c',
  install: false,
  dependencies : [glib_dep, gobject_dep, gmodule_dep, mathlib, gst_dep],
  c_args: gst_c_args,
)

# we assume that if the header is there it actually found pthreads as thread lib
threads_dep = dependency('threads', required : false)
if threads_dep.found() and cc.has_header('pthread.h')
  executable('rtpool-test', 'rtpool-test.c', 'testrtpool.c',
    install: false,
    dependencies : [glib_dep, gobject_dep, gmodule_dep, mathlib, gst_dep, threads_dep],
    c_args: gst_c_args,
  )
endif
