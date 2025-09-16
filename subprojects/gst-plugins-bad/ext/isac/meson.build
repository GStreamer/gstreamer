if cc.get_define('_MSC_VER') != ''
  webrtc_audio_coding_dep = dependency('webrtc-audio-coding-1', required: get_option('isac'), default_options: ['default_library=static'])
else
  webrtc_audio_coding_dep = dependency('webrtc-audio-coding-1', required: get_option('isac'))
endif

isac_sources = [
  'gstisac.c',
  'gstisacenc.c',
  'gstisacdec.c',
  'gstisacutils.c',
]

isac_headers = [
  'gstisacenc.h',
  'gstisacdec.h',
  'gstisacutils.h',
]

doc_sources = []
foreach s: isac_sources + isac_headers
  doc_sources += meson.current_source_dir() / s
endforeach

plugin_sources += {
  'isac': pathsep.join(doc_sources)
}

# Fedora's pkgconfig file adds -Wno-virtual-dtor and GCC complains about that
# being a C++-only argument
gstisac_c_args = cc.get_supported_arguments(['-Wno-complain-wrong-lang'])

if webrtc_audio_coding_dep.found()
  gstisac = library('gstisac', isac_sources,
    c_args : gst_plugins_bad_args + gstisac_c_args,
    include_directories : [configinc],
    dependencies : [gstaudio_dep, webrtc_audio_coding_dep],
    install : true,
    install_dir : plugins_install_dir,
  )
  plugins += [gstisac]
endif
