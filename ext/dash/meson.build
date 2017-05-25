dash_sources = [
  'gstdashdemux.c',
  'gstmpdparser.c',
  'gstplugin.c',
]

xml2_dep = dependency('libxml-2.0', version : '>= 2.8', required : false)
if xml2_dep.found()
  gstsouphttpsrc = library('gstdashdemux',
    dash_sources,
    c_args : gst_plugins_bad_args + ['-DGST_USE_UNSTABLE_API'],
    link_args : noseh_link_args,
    include_directories : [configinc, libsinc],
    dependencies : [gstadaptivedemux_dep, gsturidownloader_dep, gsttag_dep,
                    gstnet_dep, gstbase_dep, gstisoff_dep, gio_dep, xml2_dep],
    install : true,
    install_dir : plugins_install_dir,
  )
endif
