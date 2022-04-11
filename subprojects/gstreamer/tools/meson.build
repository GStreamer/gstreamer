tools = ['gst-inspect', 'gst-stats', 'gst-typefind']

extra_launch_dep = []
extra_launch_arg = []

if gst_parse
  if host_system == 'windows' and not building_for_uwp
    winmm_lib = cc.find_library('winmm', required: false)
    if winmm_lib.found() and cc.has_header('mmsystem.h')
      extra_launch_dep += [winmm_lib]
      extra_launch_arg += ['-DHAVE_WINMM']
    endif
  endif

  tools += ['gst-launch']
endif

foreach tool : tools
  exe_name = '@0@-@1@'.format(tool, apiversion)
  src_file = '@0@.c'.format(tool)
  extra_deps = []
  extra_c_args = []

  if tool == 'gst-launch'
    extra_deps += extra_launch_dep
    extra_c_args += extra_launch_arg
  endif

  executable(exe_name,
    src_file,
    install: true,
    include_directories : [configinc],
    dependencies : [glib_dep, gobject_dep, gmodule_dep, mathlib, gst_dep] + extra_deps,
    c_args: gst_c_args + extra_c_args + ['-DG_LOG_DOMAIN="@0@"'.format(exe_name)],
  )

  man_page = '@0@-1.0.1'.format(tool)
  install_man(man_page)
endforeach
