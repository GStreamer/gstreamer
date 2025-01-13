zxing_sources = [
  'gstzxing.cpp',
  'gstzxingplugin.c',
]

zxing_headers = [
  'gstzxing.h',
]

doc_sources = []
foreach s: zxing_sources + zxing_headers
  doc_sources += meson.current_source_dir() / s
endforeach

plugin_sources += {
  'zxing': pathsep.join(doc_sources)
}

zxing_dep = dependency('zxing', version : '>= 1.4.0', required : get_option('zxing'))
if zxing_dep.found()
	gstzxing = library('gstzxing',
	  zxing_sources,
	  c_args : gst_plugins_bad_args,
	  cpp_args: '-DZX_USE_UTF8',
	  include_directories : [configinc],
	  dependencies : [gstvideo_dep, zxing_dep],
	  install : true,
	  install_dir : plugins_install_dir,
	  override_options : ['cpp_std=c++17'],
	)
	plugins += [gstzxing]
endif
