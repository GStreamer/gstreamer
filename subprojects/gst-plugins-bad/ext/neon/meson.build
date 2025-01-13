neon_dep = dependency('neon', version: '>= 0.27', required: get_option('neon'))

neonhttpsrc_sources = [
  'gstneonhttpsrc.c',
]

neonhttpsrc_headers = [
  'gstneonhttpsrc.h',
]

doc_sources = []
foreach s: neonhttpsrc_sources + neonhttpsrc_headers
  doc_sources += meson.current_source_dir() / s
endforeach

plugin_sources += {
  'neonhttpsrc': pathsep.join(doc_sources)
}

if neon_dep.found()
  gstneon = library('gstneonhttpsrc',
    neonhttpsrc_sources,
    c_args : gst_plugins_bad_args,
    include_directories : [configinc],
    dependencies : [gstbase_dep, neon_dep],
    install : true,
    install_dir : plugins_install_dir)
  plugins += [gstneon]
endif
