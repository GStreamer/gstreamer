mse_sources = ['gstmse.c']

mse_headers = [
  'gstmse.h',
]

doc_sources = []
foreach s: mse_sources + mse_headers
  doc_sources += meson.current_source_dir() / s
endforeach

plugin_sources += {
  'mse': pathsep.join(doc_sources)
}

if get_option('mse').disabled()
  subdir_done()
endif

gstmse_plugin = library('gstmse',
  mse_sources,
  c_args: gst_plugins_bad_args + ['-DGST_USE_UNSTABLE_API'],
  include_directories: [configinc],
  dependencies : [gstbase_dep, gstmse_dep],
  install : true,
  install_dir : plugins_install_dir,
)

plugins += [gstmse_plugin]
