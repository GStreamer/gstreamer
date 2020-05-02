rpicamsrc_sources = [
  'gstrpicamsrc.c',
  'gstrpicamsrcdeviceprovider.c',
  'RaspiCapture.c',
  'RaspiCamControl.c',
  'RaspiPreview.c',
  'RaspiCLI.c',
]

# glib-mkenums
gnome = import('gnome')

enums = gnome.mkenums_simple('gstrpicam-enum-types',
  sources: 'gstrpicam_types.h',
  identifier_prefix: 'GstRpiCamSrc',
  symbol_prefix: 'gst_rpi_cam_src')

library('gstrpicamsrc',
  rpicamsrc_sources, enums,
  c_args : gst_rpicamsrc_args,
  include_directories : config_inc,
  dependencies : [gst_dep, gstbase_dep, gstvideo_dep] + mmal_deps,
  install : true,
  install_dir : plugins_install_dir)
