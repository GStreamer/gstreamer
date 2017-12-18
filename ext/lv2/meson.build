lv2_sources = [
  'gstlv2.c',
  'gstlv2filter.c',
  'gstlv2source.c',
  'gstlv2utils.c',
]

lv2_dep = dependency('lilv-0', version : '>= 0.22', required : false)
if lv2_dep.found()
  gstrsvg = library('gstlv2',
    lv2_sources,
    c_args : gst_plugins_bad_args,
    link_args : noseh_link_args,
    include_directories : [configinc],
    dependencies : [gstaudio_dep, gstbase_dep, lv2_dep, mathlib],
    install : true,
    install_dir : plugins_install_dir,
  )
endif
