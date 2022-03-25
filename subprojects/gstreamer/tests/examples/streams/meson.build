executable('stream-status', 'stream-status.c',
  install: false,
  dependencies : [gst_dep, gmodule_dep, mathlib],
  c_args: gst_c_args,
)

if cc.has_header('pthread.h')
  threads_dep = dependency('threads')
  executable('rtpool-test', 'rtpool-test.c', 'testrtpool.c',
    install: false,
    dependencies : [gst_dep, threads_dep, gmodule_dep, mathlib],
    c_args: gst_c_args,
  )
endif
