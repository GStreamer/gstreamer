shout2_sources = ['gstshout2.c']

shout2_headers = [
  'gstshout2.h',
]

doc_sources = []
foreach s: shout2_sources + shout2_headers
  doc_sources += meson.current_source_dir() / s
endforeach

plugin_sources += {
  'shout2': pathsep.join(doc_sources)
}

shout2_dep = dependency('shout', version : '>= 2.4.3', required : get_option('shout2'))

if shout2_dep.found()
  shout2_extra_args = []
  if shout2_dep.version().version_compare('>= 2.4.6')
    shout2_extra_args += ['-DHAVE_SHOUT_2_4_6_OR_NEWER']
  endif

  gstshout2 = library('gstshout2',
    shout2_sources,
    c_args : gst_plugins_good_args + shout2_extra_args,
    include_directories : [configinc, libsinc],
    dependencies : [gstbase_dep, shout2_dep],
    install : true,
    install_dir : plugins_install_dir,
  )
  plugins += [gstshout2]
endif
