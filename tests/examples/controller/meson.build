progs = [
  'audio-example',
  'control-sources',
  'text-color-example',
  'absolute-example',
]

foreach prog : progs
  executable(prog, prog + '.c',
    install: false,
    dependencies : [glib_dep, gobject_dep, gmodule_dep, mathlib, gst_dep, gst_controller_dep],
    c_args: gst_c_args,
  )
endforeach

gtk_dep = dependency('gtk+-3.0', required : false)
if gtk_dep.found()
  executable('controller-graph', 'controller-graph.c',
    install: false,
    dependencies : [glib_dep, gobject_dep, gmodule_dep, mathlib, gst_dep, gst_controller_dep, gtk_dep],
    c_args: gst_c_args,
  )
endif
