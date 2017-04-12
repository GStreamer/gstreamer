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

riff_deps = [audio_dep, tag_dep]
gstriff = library('gstriff-@0@'.format(api_version),
  riff_sources,
  c_args : gst_plugins_base_args,
  include_directories: [configinc, libsinc],
  version : libversion,
  soversion : soversion,
  install : true,
  dependencies : riff_deps,
  vs_module_defs: vs_module_defs_dir + 'libgstriff.def',
)

riff_gen_sources  = []

# *** GIR DISABLED for this library ***
# This library does not respect the type naming conventions required to be
# usable in binded languages.
# if build_gir
#   gst_gir_extra_args = gir_init_section + [ '--c-include=gst/riff/riff.h' ]
#   riff_gen_sources += [gnome.generate_gir(gstriff,
#     sources : riff_sources + riff_headers,
#     namespace : 'GstRiff',
#     nsversion : api_version,
#     identifier_prefix : 'Gst',
#     symbol_prefix : 'gst',
#     export_packages : 'gstreamer-riff-1.0',
#     includes : ['Gst-1.0'],
#     install : true,
#     extra_args : gst_gir_extra_args,
#     dependencies : riff_deps,
#   )]
# endif

riff_dep = declare_dependency(link_with: gstriff,
  include_directories : [libsinc],
  dependencies : riff_deps,
  sources : riff_gen_sources  )
