if get_option('vicondewarp').disabled()
  subdir_done()
endif

gstvideo_dep = dependency('gstreamer-video-1.0', version : gst_req,
    fallback : ['gst-plugins-base', 'video_dep'])

vicondewarp_dir_base = meson.current_source_dir()
vicondewarp_dir_lib = join_paths(vicondewarp_dir_base, 'lib')
lib_dewarp = cc.find_library('IMV1', dirs: vicondewarp_dir_lib, required: true)

vicondewarp_sources = [
  'gstvicondewarp.cpp'
  ]

gstvicondewarp = library('gstvicondewarp',
  vicondewarp_sources,
  dependencies : [gst_dep, gstvideo_dep, lib_dewarp],
  install : true,
  install_dir : plugins_install_dir,
)

pkgconfig.generate(gstvicondewarp, install_dir : plugins_pkgconfig_install_dir)
plugins += [gstvicondewarp]
