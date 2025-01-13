timecode_sources = [
  'plugin.c',
  'gsttimecodestamper.c',
  'gstavwait.c'
]

timecode_headers = [
  'gstavwait.h',
  'gsttimecodestamper.h',
]

doc_sources = []
foreach s: timecode_sources + timecode_headers
  doc_sources += meson.current_source_dir() / s
endforeach

plugin_sources += {
  'timecode': pathsep.join(doc_sources)
}

if get_option('timecode').disabled()
  subdir_done()
endif

tc_deps = [gstbase_dep, gstaudio_dep, gstvideo_dep]

ltc_dep = dependency('ltc', version : '>=1.1.4', required : false)
if ltc_dep.found()
  cdata.set('HAVE_LTC', 1)
  tc_deps += [ltc_dep]
else
  cdata.set('HAVE_LTC', 0)
endif

gsttimecode = library('gsttimecode',
  timecode_sources,
  c_args : gst_plugins_bad_args,
  include_directories : [configinc],
  dependencies : tc_deps,
  install : true,
  install_dir : plugins_install_dir,
)
plugins += [gsttimecode]
