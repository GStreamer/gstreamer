gst_tracers_sources = [
  'gstlatency.c',
  'gstleaks.c',
  'gststats.c',
  'gsttracers.c',
  'gstfactories.c'
]

debug_sources = [
  'gstlog.c',
]

getrusage_sources = [
  'gstrusage.c',
]

gst_tracers_headers = [
  'gstfactories.h',
  'gstlatency.h',
  'gstleaks.h',
  'gstlog.h',
  'gstrusage.h',
  'gststats.h',
]

doc_sources = []
foreach s: gst_tracers_sources + debug_sources + getrusage_sources + gst_tracers_headers
  doc_sources += meson.current_source_dir() / s
endforeach

plugin_sources += {
  'coretracers': pathsep.join(doc_sources)
}

if get_option('coretracers').disabled()
  subdir_done()
endif

if not tracer_hooks
  if get_option('coretracers').enabled()
    error('coretracers plugin enabled but not tracer_hooks')
  endif
  subdir_done()
endif

if gst_debug
  gst_tracers_sources += debug_sources
endif

if cdata.has('HAVE_GETRUSAGE')
  gst_tracers_sources += getrusage_sources
endif

thread_dep = dependency('threads', required : false)

gst_tracers = library('gstcoretracers',
  gst_tracers_sources,
  c_args : gst_c_args,
  include_directories : [configinc],
  dependencies : [gst_dep, thread_dep],
  install : true,
  install_dir : plugins_install_dir,
)
plugins += [gst_tracers]
