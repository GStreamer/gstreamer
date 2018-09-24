gstadaptivedemux = library('gstadaptivedemux-' + api_version,
  'gstadaptivedemux.c',
  c_args : gst_plugins_bad_args + ['-DGST_USE_UNSTABLE_API', '-DBUILDING_GST_ADAPTIVE_DEMUX'],
  include_directories : [configinc, libsinc],
  version : libversion,
  soversion : soversion,
  darwin_versions : osxversion,
  install : true,
  dependencies : [gstbase_dep, gsturidownloader_dep],
)

gstadaptivedemux_dep = declare_dependency(link_with : gstadaptivedemux,
  include_directories : [libsinc],
  dependencies : [gstbase_dep, gsturidownloader_dep])
