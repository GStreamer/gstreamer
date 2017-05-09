webrtc_sources = [
  'gstwebrtcdsp.cpp',
  'gstwebrtcechoprobe.cpp'
]

webrtc_dep = dependency('webrtc-audio-processing', version : ['>= 0.2', '< 0.4'], required : false)

if webrtc_dep.found()
  gstwebrtcdsp = library('gstwebrtcdsp',
    webrtc_sources,
    cpp_args : gst_plugins_bad_args,
    link_args : noseh_link_args,
    include_directories : [configinc],
    dependencies : [gstbase_dep, gstaudio_dep, webrtc_dep],
    install : true,
    install_dir : plugins_install_dir,
  )
endif
