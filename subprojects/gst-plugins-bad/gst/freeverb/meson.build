freeverb_sources = [
  'gstfreeverb.c',
]

freeverb_headers = [
  'gstfreeverb.h',
]

doc_sources = []
foreach s: freeverb_sources + freeverb_headers
  doc_sources += meson.current_source_dir() / s
endforeach

plugin_sources += {
  'freeverb': pathsep.join(doc_sources)
}

if get_option('freeverb').disabled()
  subdir_done()
endif

gstfreeverb = library('gstfreeverb',
  freeverb_sources,
  c_args : gst_plugins_bad_args,
  include_directories : [configinc],
  dependencies : [gstbase_dep, gstaudio_dep],
  install : true,
  install_dir : plugins_install_dir,
)
plugins += [gstfreeverb]

install_data('GstFreeverb.prs', install_dir: presetdir)

env = environment()
env.prepend('GST_PRESET_PATH', meson.current_source_dir())
meson.add_devenv(env)
