gst_tracers_sources = [
  'gstlatency.c',
  'gstleaks.c',
  'gststats.c',
  'gsttracers.c',
  'gstlog.c'
]

if have_getrusage
  gst_tracers_sources += ['gstrusage.c']
endif

gst_tracers = library('gstcoretracers',
  gst_tracers_sources,
  c_args : gst_c_args + ['-DGST_USE_UNSTABLE_API'],
  include_directories : [configinc],
  dependencies : [gst_dep, unwind_dep],
  link_with : printf_lib,
  install : true,
  install_dir : '@0@/gstreamer-1.0'.format(get_option('libdir')),
)

