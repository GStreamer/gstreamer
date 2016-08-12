bayer_sources = [
  'gstbayer.c',
  'gstbayer2rgb.c',
  'gstrgb2bayer.c',
]

orcsrc = 'gstbayerorc'
if have_orcc
  orc_h = custom_target(orcsrc + '.h',
    input : orcsrc + '.orc',
    output : orcsrc + '.h',
    command : orcc_args + ['--header', '-o', '@OUTPUT@', '@INPUT@'])
  orc_c = custom_target(orcsrc + '.c',
    input : orcsrc + '.orc',
    output : orcsrc + '.c',
    command : orcc_args + ['--implementation', '-o', '@OUTPUT@', '@INPUT@'])
else
  orc_h = configure_file(input : orcsrc + '-dist.h',
    output : orcsrc + '.h',
    configuration : configuration_data())
  orc_c = configure_file(input : orcsrc + '-dist.c',
    output : orcsrc + '.c',
    configuration : configuration_data())
endif

gstbayer = library('gstbayer',
  bayer_sources, orc_c, orc_h,
  c_args : gst_plugins_bad_args,
  include_directories : [configinc, libsinc],
  dependencies : [gstbase_dep, gstvideo_dep, orc_dep],
  install : true,
  install_dir : plugins_install_dir,
)
