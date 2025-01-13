twolame_dep = dependency('twolame', version : '>= 0.3.10', required : get_option('twolame'))

twolame_sources = [
  'gsttwolamemp2enc.c',
]

twolame_headers = [
  'gsttwolamemp2enc.h',
]

doc_sources = []
foreach s: twolame_sources + twolame_headers
  doc_sources += meson.current_source_dir() / s
endforeach

plugin_sources += {
  'twolame': pathsep.join(doc_sources)
}

if twolame_dep.found()
  twolame = library('gsttwolame',
    twolame_sources,
    c_args : gst_plugins_good_args,
    include_directories : [configinc, libsinc],
    dependencies : [gstaudio_dep, twolame_dep],
    install : true,
    install_dir : plugins_install_dir,
  )
  plugins += [twolame]
endif
