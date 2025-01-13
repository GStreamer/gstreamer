spectrum_sources = [
  'gstspectrum.c',
]

spectrum_headers = [
  'gstspectrum.h',
]

doc_sources = []
foreach s: spectrum_sources + spectrum_headers
  doc_sources += meson.current_source_dir() / s
endforeach

plugin_sources += {
  'spectrum': pathsep.join(doc_sources)
}

if get_option('spectrum').disabled()
  subdir_done()
endif

gstspectrum = library('gstspectrum',
  spectrum_sources,
  c_args : gst_plugins_good_args,
  include_directories : [configinc],
  dependencies : [gstbase_dep, gstfft_dep, gstaudio_dep, libm],
  install : true,
  install_dir : plugins_install_dir,
)
plugins += [gstspectrum]
