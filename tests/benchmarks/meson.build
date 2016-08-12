benchmarks = [
  'caps',
  'capsnego',
  'complexity',
  'controller',
  'init',
  'mass-elements',
  'gstpollstress',
  'gstpoolstress',
  'gstclockstress',
  'gstbufferstress',
]

foreach b : benchmarks
  executable(b, '@0@.c'.format(b),
    c_args : gst_c_args,
    link_with : [printf_lib],
    dependencies : [gobject_dep, gmodule_dep, glib_dep, gst_dep, gst_controller_dep],
    )
endforeach
