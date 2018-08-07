wasapi_sources = [
  'gstwasapi.c',
  'gstwasapisrc.c',
  'gstwasapisink.c',
  'gstwasapiutil.c',
  'gstwasapidevice.c',
]

if host_system == 'windows' and cc.has_header('audioclient.h')
  wasapi_dep = [cc.find_library('ole32'), cc.find_library('ksuser'),
                cc.find_library('avrt', required : false)]

  wasapi_args = ['-DCOBJMACROS']
  if cc.has_header_symbol('audioclient.h', 'AUDCLNT_STREAMOPTIONS_NONE')
    wasapi_args += ['-DHAVE_AUDCLNT_STREAMOPTIONS']
  endif

  gstwasapi = library('gstwasapi',
    wasapi_sources,
    c_args : gst_plugins_bad_args + wasapi_args,
    include_directories : [configinc],
    dependencies : [gstaudio_dep] + wasapi_dep,
    install : true,
    install_dir : plugins_install_dir)
endif
