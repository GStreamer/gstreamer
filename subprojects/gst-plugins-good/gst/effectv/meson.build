effect_sources = [
  'gsteffectv.c', 'gstedge.c', 'gstaging.c', 'gstdice.c', 'gstwarp.c',
  'gstshagadelic.c', 'gstvertigo.c', 'gstrev.c', 'gstquark.c', 'gstop.c',
  'gstradioac.c', 'gststreak.c', 'gstripple.c'
]

effect_headers = [
  'gstop.h',
  'gstdice.h',
  'gstrev.h',
  'gstwarp.h',
  'gstaging.h',
  'gstquark.h',
  'gsteffectv.h',
  'gstshagadelic.h',
  'gstripple.h',
  'gstradioac.h',
  'gstedge.h',
  'gststreak.h',
  'gstvertigo.h',
]

doc_sources = []
foreach s: effect_sources + effect_headers
  doc_sources += meson.current_source_dir() / s
endforeach

plugin_sources += {
  'effectv': pathsep.join(doc_sources)
}

if get_option('effectv').disabled()
  subdir_done()
endif

gsteffectv = library('gsteffectv',
  effect_sources,
  c_args : gst_plugins_good_args,
  include_directories : [configinc],
  dependencies : [gst_dep, gstbase_dep, gstvideo_dep, libm],
  install : true,
  install_dir : plugins_install_dir,
)
plugins += [gsteffectv]
