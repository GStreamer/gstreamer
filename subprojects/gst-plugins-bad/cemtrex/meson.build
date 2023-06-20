plugin_c_args = ['-DHAVE_CONFIG_H']



plugin_dep = dependency('gstreamer-video-1.0',
    fallback: ['gst-plugins-base', 'audio_dep'])

dir_base = meson.current_source_dir()
dir_lib = join_paths(dir_base, 'lib')
lib_dewarp = cc.find_library('IMV1', dirs: dir_lib, required: true)

# Plugin 1
plugin_sources = [
  'gstvicondewarp.cpp'
  ]

gstpluginexample = library('gstvicondewarp',
  plugin_sources,
  c_args: plugin_c_args,
  dependencies : [gst_dep, plugin_dep, lib_dewarp],
  install : true,
  install_dir : plugins_install_dir,
)