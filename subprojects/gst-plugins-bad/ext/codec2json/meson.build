codec2json_sources = files([
  'gstav12json.c',
  'gstvp82json.c',
  'plugin.c',
])

cp_args = [
  '-DGST_USE_UNSTABLE_API',
  '-DBUILDING_GST_CODECS',
  '-DG_LOG_DOMAIN="GStreamer-Codec2json"'
]

json_dep = dependency('json-glib-1.0', version : '>=1.6.6', fallback : ['json-glib', 'json_glib_dep'], required: get_option('codec2json'))

if json_dep.found()
  gstcodec2json = library('gstcodec2json',
    codec2json_sources,
    c_args : gst_plugins_bad_args + cp_args,
    include_directories : [configinc],
    version : libversion,
    soversion : soversion,
    darwin_versions : osxversion,
    install : true,
    install_dir : plugins_install_dir,
    dependencies : [gstvideo_dep, gstcodecs_dep, json_dep],
  )
  plugins += [gstcodec2json]
endif
