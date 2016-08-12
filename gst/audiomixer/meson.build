audiomixer_sources = [
  'gstaudiomixer.c',
  'gstaudiointerleave.c',
]

orcsrc = 'gstaudiomixerorc'
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

gstaudiomixer = library('gstaudiomixer',
  audiomixer_sources, orc_c, orc_h,
  c_args : gst_plugins_bad_args + [ '-DGST_USE_UNSTABLE_API' ],
  include_directories : [configinc],
  dependencies : [gstbadaudio_dep, gstbadbase_dep, gstaudio_dep, gstbase_dep,
		  orc_dep],
  install : true,
  install_dir : plugins_install_dir,
)
