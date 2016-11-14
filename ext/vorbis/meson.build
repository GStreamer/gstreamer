vorbis_sources = [
  'gstvorbis.c',
  'gstvorbisdec.c',
  'gstvorbisdeclib.c',
  'gstvorbisenc.c',
  'gstvorbisparse.c',
  'gstvorbistag.c',
  'gstvorbiscommon.c',
]

vorbisidec_sources = [
  'gstivorbisdec.c',
  'gstvorbisdec.c',
  'gstvorbisdeclib.c',
  'gstvorbiscommon.c',
]

vorbis_dep = dependency('vorbis', version : '>=1.0', required : false)
vorbisenc_dep = dependency('vorbisenc', version : '>=1.0', required : false)
vorbisidec_dep = dependency('vorbisidec', required : false)

if vorbis_dep.found() or vorbisidec_dep.found()
  if cc.has_header_symbol('vorbis/codec.h', 'vorbis_synthesis_restart')
    core_conf.set('HAVE_VORBIS_SYNTHESIS_RESTART', true)
  endif
endif

if vorbis_dep.found()
  vorbis_deps = [vorbis_dep]
  if vorbisenc_dep.found()
    vorbis_deps += vorbisenc_dep
  endif
  gstvorbis = library('gstvorbis',
    vorbis_sources,
    c_args : gst_plugins_base_args,
    link_args : noseh_link_args,
    include_directories: [configinc, libsinc],
    dependencies : vorbis_deps + glib_deps + [audio_dep, tag_dep, gst_dep, gst_base_dep],
    install : true,
    install_dir : plugins_install_dir,
  )
endif

if vorbisidec_dep.found()
  gstivorbisdec = library('gstivorbisdec',
    vorbisidec_sources,
    c_args : gst_plugins_base_args + ['-DTREMOR'],
    link_args : noseh_link_args,
    include_directories: [configinc, libsinc],
    dependencies : glib_deps + [vorbisidec_dep, audio_dep, tag_dep, gst_dep, gst_base_dep],
    install : true,
    install_dir : plugins_install_dir,
  )
endif
