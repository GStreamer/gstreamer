midi_sources = [
  'midi.c',
  'midiparse.c',
]

midi_headers = [
  'midiparse.h',
]

doc_sources = []
foreach s: midi_sources + midi_headers
  doc_sources += meson.current_source_dir() / s
endforeach

plugin_sources += {
  'midi': pathsep.join(doc_sources)
}

if get_option('midi').disabled()
  subdir_done()
endif

gstmidi = library('gstmidi',
  midi_sources,
  c_args : gst_plugins_bad_args,
  include_directories : [configinc, libsinc],
  dependencies : [gstbase_dep, gsttag_dep, libm],
  install : true,
  install_dir : plugins_install_dir,
)
plugins += [gstmidi]
