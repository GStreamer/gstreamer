progs = [
  'audio-example',
  'control-sources',
  'text-color-example',
  'absolute-example',
]

foreach prog : progs
  executable(prog, prog + '.c',
    install: false,
    dependencies : [gst_dep, gst_controller_dep, gmodule_dep, mathlib],
    c_args: gst_c_args,
  )
endforeach

gtk_dep = dependency('gtk+-3.0', required : get_option('examples'))
if gtk_dep.found()
  executable('controller-graph', 'controller-graph.c',
    install: false,
    dependencies : [gst_dep, gst_controller_dep, gmodule_dep, mathlib, gtk_dep],
    c_args: gst_c_args,
  )
endif
