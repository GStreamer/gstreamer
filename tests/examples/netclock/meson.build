executable('netclock-server', 'netclock-server.c',
  install: false,
  dependencies : [glib_dep, gobject_dep, gmodule_dep, mathlib, gst_dep, gst_net_dep],
  c_args: gst_c_args,
)

executable('netclock-client', 'netclock-client.c',
  install: false,
  dependencies : [glib_dep, gobject_dep, gmodule_dep, mathlib, gst_dep, gst_net_dep],
  c_args: gst_c_args,
)
