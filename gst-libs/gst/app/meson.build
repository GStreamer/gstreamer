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
# TODO: generate gir

app_dep = declare_dependency(link_with: gstapp,
  include_directories : [libsinc],
  dependencies : [gst_base_dep])
