wavparse_sources = [
  'gstwavparse.c',
]

wavparse_headers = [
  'gstwavparse.h',
]

doc_sources = []
foreach s: wavparse_sources + wavparse_headers
  doc_sources += meson.current_source_dir() / s
endforeach

plugin_sources += {
  'wavparse': pathsep.join(doc_sources)
}

if get_option('wavparse').disabled()
  subdir_done()
endif

gstwawparse = library('gstwavparse',
  wavparse_sources,
  c_args : gst_plugins_good_args,
  include_directories : [configinc, libsinc],
  dependencies : [gstbase_dep, gstpbutils_dep, gstriff_dep, gstaudio_dep,
		  gsttag_dep, libm],
  install : true,
  install_dir : plugins_install_dir,
)
plugins += [gstwawparse]
