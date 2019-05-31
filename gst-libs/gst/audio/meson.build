badaudio_sources = ['gstnonstreamaudiodecoder.c', 'gstplanaraudioadapter.c']
badaudio_headers = ['gstnonstreamaudiodecoder.h', 'audio-bad-prelude.h', 'gstplanaraudioadapter.h']
install_headers(badaudio_headers, subdir : 'gstreamer-1.0/gst/audio')


gstbadaudio = library('gstbadaudio-' + api_version,
  badaudio_sources,
  c_args : gst_plugins_bad_args + ['-DGST_USE_UNSTABLE_API', '-DBUILDING_GST_AUDIO_BAD'],
  include_directories : [configinc, libsinc],
  version : libversion,
  soversion : soversion,
  darwin_versions : osxversion,
  install : true,
  dependencies : [gstaudio_dep, gstbase_dep],
)

gstbadaudio_dep = declare_dependency(link_with : gstbadaudio,
  include_directories : [libsinc],
  dependencies : [gstaudio_dep, gstbase_dep])

if build_gir
  audio_gir = gnome.generate_gir(gstbadaudio,
    sources : badaudio_sources + badaudio_headers,
    namespace : 'GstBadAudio',
    nsversion : api_version,
    identifier_prefix : 'Gst',
    symbol_prefix : 'gst',
    export_packages : 'gstreamer-bad-audio-1.0',
    includes : ['Gst-1.0', 'GstAudio-1.0', 'GstBase-1.0'],
    install : true,
    extra_args : gir_init_section + ['-DGST_USE_UNSTABLE_API'],
    dependencies : [gstbase_dep, gstaudio_dep]
  )
endif
