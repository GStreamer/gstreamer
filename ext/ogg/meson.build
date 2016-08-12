ogg_sources = [
  'dirac_parse.c',
  'gstoggaviparse.c',
  'gstogg.c',
  'gstoggdemux.c',
  'gstoggmux.c',
  'gstoggparse.c',
  'gstoggstream.c',
  'gstogmparse.c',
  'vorbis_parse.c',
]

ogg_dep = dependency('ogg', version : '>=1.0', required : false)
core_conf.set('HAVE_OGG', ogg_dep.found())

if ogg_dep.found()
  gstogg = library('gstogg',
    ogg_sources,
    c_args : gst_plugins_base_args,
    link_args : noseh_link_args,
    include_directories: [configinc, libsinc],
    dependencies : glib_deps + [ogg_dep, audio_dep, pbutils_dep, tag_dep, riff_dep, gst_dep, gst_base_dep],
    install : true,
    install_dir : plugins_install_dir,
  )
endif
