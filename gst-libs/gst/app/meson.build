app_sources = ['gstappsrc.c', 'gstappsink.c']

app_headers = [ 'app.h', 'gstappsrc.h', 'gstappsink.h' ]
install_headers(app_headers, subdir : 'gstreamer-1.0/gst/app/')

gstapp = library('gstapp-@0@'.format(api_version),
  app_sources,
  c_args : gst_plugins_base_args,
  include_directories: [configinc, libsinc],
  version : libversion,
  soversion : soversion,
  install : true,
  dependencies : [gst_base_dep],
  vs_module_defs: vs_module_defs_dir + 'libgstapp.def',
)

app_gen_sources = []
if build_gir
  app_gen_sources += [gnome.generate_gir(gstapp,
    sources : app_sources + app_headers,
    namespace : 'GstApp',
    nsversion : api_version,
    identifier_prefix : 'Gst',
    symbol_prefix : 'gst',
    export_packages : 'gstreamer-app-1.0',
    includes : ['Gst-1.0', 'GstBase-1.0'],
    install : true,
    extra_args : gir_init_section,
    dependencies : [gst_dep, gst_base_dep]
  )]
endif

app_dep = declare_dependency(link_with: gstapp,
  include_directories : [libsinc],
  dependencies : [gst_base_dep],
  sources : app_gen_sources)
