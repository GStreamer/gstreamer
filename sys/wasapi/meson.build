wasapi_sources = [
  'gstwasapi.c',
  'gstwasapisrc.c',
  'gstwasapisink.c',
  'gstwasapiutil.c',
]

if host_system == 'windows' and cc.has_header('audioclient.h')
  wasapi_dep = [cc.find_library('ole32'), cc.find_library('ksuser')]

  gstwasapi = library('gstwasapi',
    wasapi_sources,
    c_args : gst_plugins_bad_args + ['-DCOBJMACROS'],
    include_directories : [configinc],
    dependencies : [gstaudio_dep] + wasapi_dep,
    install : true,
    install_dir : plugins_install_dir)
endif
