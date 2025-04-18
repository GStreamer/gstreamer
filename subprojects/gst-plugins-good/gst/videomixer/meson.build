vmixer_sources = [
  'blend.c',
  'videomixer2.c',
]

vmixer_headers = [
  'videomixer2pad.h',
  'blend.h',
  'videomixer2.h',
]

doc_sources = []
foreach s: vmixer_sources + vmixer_headers
  doc_sources += meson.current_source_dir() / s
endforeach

plugin_sources += {
  'videomixer': pathsep.join(doc_sources)
}

if get_option('videomixer').disabled()
  subdir_done()
endif

orcsrc = 'videomixerorc'
if have_orcc
  orc_h = custom_target(orcsrc + '.h',
    input : orcsrc + '.orc',
    output : orcsrc + '.h',
    command : orcc_args + ['--header', '-o', '@OUTPUT@', '@INPUT@'])
  orc_c = custom_target(orcsrc + '.c',
    input : orcsrc + '.orc',
    output : orcsrc + '.c',
    command : orcc_args + ['--implementation', '-o', '@OUTPUT@', '@INPUT@'])
  orc_targets += {'name': orcsrc, 'orc-source': files(orcsrc + '.orc'), 'header': orc_h, 'source': orc_c}
else
  orc_h = configure_file(input : orcsrc + '-dist.h',
    output : orcsrc + '.h',
    copy : true)
  orc_c = configure_file(input : orcsrc + '-dist.c',
    output : orcsrc + '.c',
    copy : true)
endif

gstvideomixer = library('gstvideomixer',
  vmixer_sources, orc_c, orc_h,
  c_args : gst_plugins_good_args,
  include_directories : [configinc],
  dependencies : [orc_dep, gstvideo_dep, gstbase_dep, libm],
  install : true,
  install_dir : plugins_install_dir,
)
plugins += [gstvideomixer]
