navigationtest_sources = [
  'gstnavigationtest.c',
]

navigationtest_headers = [
  'gstnavigationtest.h',
]

doc_sources = []
foreach s: navigationtest_sources + navigationtest_headers
  doc_sources += meson.current_source_dir() / s
endforeach

plugin_sources += {
  'navigationtest': pathsep.join(doc_sources)
}

debug_sources = [
  'gstdebug.c',
  'breakmydata.c',
  'gstcapssetter.c',
  'gstnavseek.c',
  'gstpushfilesrc.c',
  'gsttaginject.c',
  'rndbuffersize.c',
  'progressreport.c',
  'tests.c',
  'cpureport.c',
  'testplugin.c',
]

debug_headers = [
  'cpureport.h',
  'gstcapsdebug.h',
  'gstcapssetter.h',
  'gstdebugutilselements.h',
  'gstnavseek.h',
  'gstpushfilesrc.h',
  'gsttaginject.h',
  'progressreport.h',
]

doc_sources = []
foreach s: debug_sources + debug_headers
  doc_sources += meson.current_source_dir() / s
endforeach

plugin_sources += {
  'debug': pathsep.join(doc_sources)
}

if get_option('debugutils').disabled()
  subdir_done()
endif

gstnavigationtest = library('gstnavigationtest',
  navigationtest_sources,
  c_args : gst_plugins_good_args,
  include_directories : [configinc],
  dependencies : [gstbase_dep, gstvideo_dep, libm],
  install : true,
  install_dir : plugins_install_dir,
)
plugins += [gstnavigationtest]

gstdebug = library('gstdebug',
  debug_sources,
  c_args: gst_plugins_good_args,
  include_directories : [configinc],
  dependencies : [gst_dep, gstbase_dep, gstvideo_dep],
  install : true,
  install_dir : plugins_install_dir,
)
plugins += [gstdebug]
