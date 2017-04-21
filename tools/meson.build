tools = [ 'gst-inspect', 'gst-launch', 'gst-stats', 'gst-typefind' ]

foreach tool : tools
  exe_name = '@0@-@1@'.format(tool, apiversion)
  src_file = '@0@.c'.format(tool)

  executable(exe_name,
    src_file,
    install: true,
    dependencies : [glib_dep, gobject_dep, gmodule_dep, mathlib, gst_dep],
    link_with: [printf_lib],
    c_args: gst_c_args,
  )

  man_page = '@0@-1.0.1'.format(tool)
  install_man(man_page)
endforeach
