rpicamsrc_sources = [
  'gstrpicamsrc.c',
  'gstrpicamsrcdeviceprovider.c',
  'RaspiCapture.c',
  'RaspiCamControl.c',
  'RaspiPreview.c',
  'RaspiCLI.c',
]

# This can be simplified once meson gets native support for glib-mkenums (soon)
glib_mkenums = find_program('glib-mkenums')

gstrpicam_enum_types_h = custom_target('gstrpicam-enum-types.h',
  output : 'gstrpicam-enum-types.h',
  input : files('gstrpicam_types.h'),
  command : [glib_mkenums, '--template', meson.current_source_dir() + '/gstrpicam-enums-template.h', '@INPUT@'],
  capture : true)

gstrpicam_enum_types_c = custom_target('gstrpicam-enum-types.c',
  output : 'gstrpicam-enum-types.c',
  input : files('gstrpicam_types.h'),
  depends : [gstrpicam_enum_types_h],
  command : [glib_mkenums, '--template', meson.current_source_dir() + '/gstrpicam-enums-template.c', '@INPUT@'],
  capture : true)

library('gstrpicamsrc',
  rpicamsrc_sources, gstrpicam_enum_types_h, gstrpicam_enum_types_c,
  c_args : gst_rpicamsrc_args,
  include_directories : config_inc,
  dependencies : [gst_dep, gstbase_dep, gstvideo_dep] + mmal_deps,
  install : true,
  install_dir : plugins_install_dir)
