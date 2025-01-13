goom_sources = [
  'gstgoom.c',
  'drawmethods.c',
  'sound_tester.c',
  'mathtools.c',
  'lines.c',
  'ifs.c',
  'surf3d.c',
  'tentacle3d.c',
  'v3d.c',
  'convolve_fx.c',
  'flying_stars_fx.c',
  'plugin_info.c',
  'goom_tools.c',
  'config_param.c',
  'filters.c',
  'goom_core.c',
  'graphic.c',
]

goom_headers = [
  'surf3d.h',
  'gstgoom.h',
  'goom.h',
  'ppc_drawings.h',
  'lines.h',
  'ifs.h',
  'sound_tester.h',
  'v3d.h',
  'motif_goom1.h',
  'drawmethods.h',
  'ppc_zoom_ultimate.h',
  'goom_typedefs.h',
  'goom_plugin_info.h',
  'mmx.h',
  'goom_tools.h',
  'mathtools.h',
  'goom_config_param.h',
  'xmmx.h',
  'motif_goom2.h',
  'tentacle3d.h',
  'goom_graphic.h',
  'goom_config.h',
  'goom_visual_fx.h',
  'goom_fx.h',
  'goom_filters.h',
]

doc_sources = []
foreach s: goom_sources + goom_headers
  doc_sources += meson.current_source_dir() / s
endforeach

plugin_sources += {
  'goom': pathsep.join(doc_sources)
}

if get_option('goom').disabled()
  subdir_done()
endif

gstgoom = library('gstgoom',
  goom_sources,
  c_args : gst_plugins_good_args,
  include_directories : [configinc],
  dependencies : [gst_dep, gstpbutils_dep, gstbase_dep, orc_dep, libm],
  install : true,
  install_dir : plugins_install_dir,
)
plugins += [gstgoom]
