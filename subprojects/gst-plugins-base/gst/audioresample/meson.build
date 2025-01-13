resample_sources = ['gstaudioresample.c']

resample_headers = [
  'gstaudioresample.h',
]

doc_sources = []
foreach s: resample_sources + resample_headers
  doc_sources += meson.current_source_dir() / s
endforeach

plugin_sources += {
  'audioresample': pathsep.join(doc_sources)
}

if get_option('audioresample').disabled()
  subdir_done()
endif

audioresample = library('gstaudioresample',
  resample_sources,
  c_args : gst_plugins_base_args,
  include_directories: [configinc, libsinc],
  dependencies : [audio_dep, gst_base_dep, libm],
  install : true,
  install_dir : plugins_install_dir,
)
plugins += [audioresample]
