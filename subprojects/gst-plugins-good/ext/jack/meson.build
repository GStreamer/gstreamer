jack_sources = [
  'gstjackaudioclient.c',
  'gstjackaudiosink.c',
  'gstjackaudiosrc.c',
  'gstjack.c',
  'gstjackutil.c',
  'gstjackloader.c',
]

jack_headers = [
  'gstjackringbuffer.h',
  'gstjack.h',
  'gstjackloader.h',
  'gstjackaudiosrc.h',
  'gstjackaudioclient.h',
  'gstjackutil.h',
  'gstjackaudiosink.h',
]

doc_sources = []
foreach s: jack_sources + jack_headers
  doc_sources += meson.current_source_dir() / s
endforeach

plugin_sources += {
  'jack': pathsep.join(doc_sources)
}

jack_option = get_option('jack')
if jack_option.disabled()
  subdir_done()
endif

gstjack = library('gstjack',
  jack_sources,
  c_args : gst_plugins_good_args,
  include_directories : [configinc],
  dependencies : [gst_dep, gstbase_dep, gstaudio_dep, gmodule_dep],
  install : true,
  install_dir : plugins_install_dir,
)
plugins += [gstjack]
