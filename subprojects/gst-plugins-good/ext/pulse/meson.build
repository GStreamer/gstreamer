pulse_sources = [
  'gstpulseelement.c',
  'plugin.c',
  'pulsesink.c',
  'pulsesrc.c',
  'pulsedeviceprovider.c',
  'pulseutil.c',
]

pulse_headers = [
  'pulsesink.h',
  'pulseutil.h',
  'pulsedeviceprovider.h',
  'gstpulseelements.h',
  'pulsesrc.h',
]

doc_sources = []
foreach s: pulse_sources + pulse_headers
  doc_sources += meson.current_source_dir() / s
endforeach

plugin_sources += {
  'pulseaudio': pathsep.join(doc_sources)
}

libpulse_dep = dependency('libpulse', version : '>=2.0', required : get_option('pulse'))

if libpulse_dep.found()
  gstpulse = library('gstpulseaudio',
    pulse_sources,
    c_args : gst_plugins_good_args,
    include_directories : [configinc, libsinc],
    dependencies : [gst_dep, gstbase_dep, gstaudio_dep, gstpbutils_dep, libpulse_dep],
    install : true,
    install_dir : plugins_install_dir,
  )
  plugins += [gstpulse]
endif
