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

faac_dep = cc.find_library('faac', required : get_option('faac'))
have_faac_h = cc.has_header_symbol('faac.h', 'faacEncOpen')
if not have_faac_h and get_option('faac').enabled()
  error('faac plugin enabled: found libfaac, but not faac.h')
endif

if faac_dep.found() and have_faac_h
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
