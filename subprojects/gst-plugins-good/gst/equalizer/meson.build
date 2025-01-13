eq_sources = [
  'gstiirequalizer.c',
  'gstiirequalizerplugin.c',
  'gstiirequalizernbands.c',
  'gstiirequalizer3bands.c',
  'gstiirequalizer10bands.c',
  'gstiirequalizerplugin.c',
]

eq_headers = [
  'gstiirequalizernbands.h',
  'gstiirequalizer3bands.h',
  'gstiirequalizer.h',
  'gstiirequalizer10bands.h',
]

doc_sources = []
foreach s: eq_sources + eq_headers
  doc_sources += meson.current_source_dir() / s
endforeach

plugin_sources += {
  'equalizer': pathsep.join(doc_sources)
}

if get_option('equalizer').disabled()
  subdir_done()
endif

gstequalizer = library('gstequalizer',
  eq_sources,
  c_args : gst_plugins_good_args,
  include_directories : [configinc, libsinc],
  dependencies : [gstbase_dep, gstaudio_dep, libm],
  install : true,
  install_dir : plugins_install_dir,
)
plugins += [gstequalizer]

install_data(sources: ['GstIirEqualizer3Bands.prs',
  'GstIirEqualizer10Bands.prs'], install_dir: presetdir)

env = environment()
env.prepend('GST_PRESET_PATH', meson.current_source_dir())
meson.add_devenv(env)
