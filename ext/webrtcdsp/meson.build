webrtc_sources = [
  'gstwebrtcdsp.cpp',
  'gstwebrtcechoprobe.cpp'
]

webrtc_dep = dependency('webrtc-audio-processing', version : ['>= 0.2', '< 0.4'],
                        required : get_option('webrtcdsp'))

gnustl_dep = declare_dependency()
if host_system == 'android'
  gnustl_dep = dependency('gnustl', required : get_option('webrtcdsp'))
endif

if webrtc_dep.found() and gnustl_dep.found()
  gstwebrtcdsp = library('gstwebrtcdsp',
    webrtc_sources,
    cpp_args : gst_plugins_bad_args,
    link_args : noseh_link_args,
    include_directories : [configinc],
    dependencies : [gstbase_dep, gstaudio_dep, gstbadaudio_dep, webrtc_dep, gnustl_dep],
    install : true,
    install_dir : plugins_install_dir,
  )
  pkgconfig.generate(gstwebrtcdsp, install_dir : plugins_pkgconfig_install_dir)
endif
