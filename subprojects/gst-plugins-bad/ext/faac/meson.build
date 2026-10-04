faac_sources = [
  'gstfaac.c',
]

faac_headers = [
  'gstfaac.h',
]

doc_sources = []
foreach s: faac_sources + faac_headers
  doc_sources += meson.current_source_dir() / s
endforeach

plugin_sources += {
  'faac': pathsep.join(doc_sources)
}

if get_option('faac').disabled()
  faac_dep = dependency('', required: false)
  subdir_done()
endif

# faac >= 1.31 ships a pkg-config file, so try that first
faac_dep = dependency('faac', required : false)
if not faac_dep.found() and (cc.has_header_symbol('faac.h', 'faacEncOpen') or cc.has_header_symbol('faac.h', 'faac_encoder_open'))
  faac_dep = cc.find_library('faac', required : false)
endif
if not faac_dep.found() and get_option('faac').enabled()
  error('faac plugin enabled, but libfaac not found')
endif

if faac_dep.found()
  gstfaac = library('gstfaac',
    faac_sources,
    c_args : gst_plugins_bad_args,
    include_directories : [configinc],
    dependencies : [gstaudio_dep, gstpbutils_dep, gsttag_dep, faac_dep],
    install : true,
    install_dir : plugins_install_dir,
  )
  plugins += [gstfaac]
endif
