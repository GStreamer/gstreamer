freeverb_sources = [
  'gstfreeverb.c',
]

gstfreeverb = library('gstfreeverb',
  freeverb_sources,
  c_args : gst_plugins_bad_args,
  include_directories : [configinc],
  dependencies : [gstbase_dep, gstaudio_dep],
  install : true,
  install_dir : plugins_install_dir,
)
pkgconfig.generate(gstfreeverb, install_dir : plugins_pkgconfig_install_dir)

install_data('GstFreeverb.prs', install_dir: presetdir)
