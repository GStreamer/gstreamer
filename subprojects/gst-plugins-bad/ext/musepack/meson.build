mpcdec_dep = cc.find_library('mpcdec', required: get_option('musepack'))

musepack_sources = [
  'gstmusepackdec.c', 
  'gstmusepackreader.c',
]

musepack_headers = [
  'gstmusepackreader.h',
  'gstmusepackdec.h',
]

doc_sources = []
foreach s: musepack_sources + musepack_headers
  doc_sources += meson.current_source_dir() / s
endforeach

plugin_sources += {
  'musepack': pathsep.join(doc_sources)
}

if mpcdec_dep.found() and cc.has_header_symbol('mpc/mpcdec.h', 'mpc_demux_init', dependencies: mpcdec_dep)
  gstmusepack = library('gstmusepack',
    musepack_sources,
    c_args : gst_plugins_bad_args,
    link_args : noseh_link_args,
    include_directories : [configinc],
    dependencies : [gstaudio_dep, mpcdec_dep],
    install : true,
    install_dir : plugins_install_dir,
  )
  plugins += [gstmusepack]
elif get_option('musepack').enabled()
  error('Could not find libmpcdec header files, but Musepack was enabled via options')
endif
