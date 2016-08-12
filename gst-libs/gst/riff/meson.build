riff_sources = [
  'riff.c',
  'riff-media.c',
  'riff-read.c',
]

riff_headers = [
  'riff.h',
  'riff-ids.h',
  'riff-media.h',
  'riff-read.h',
]
install_headers(riff_headers, subdir : 'gstreamer-1.0/gst/riff/')

gstriff = library('gstriff-@0@'.format(api_version),
  riff_sources,
  c_args : gst_plugins_base_args,
  include_directories: [configinc, libsinc],
  version : libversion,
  soversion : soversion,
  install : true,
  dependencies : [audio_dep, tag_dep],
  vs_module_defs: vs_module_defs_dir + 'libgstriff.def',
)
# TODO: generate gir

riff_dep = declare_dependency(link_with: gstriff,
  include_directories : [libsinc],
  dependencies : [audio_dep, tag_dep])
