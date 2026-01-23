mpeghuimanager_sources = [
  'gstmpeghuimanager.c'
]

mpeghuimanager_headers = [
  'gstmpeghuimanager.h'
]

doc_sources = []
foreach s: mpeghuimanager_sources + mpeghuimanager_headers
  doc_sources += meson.current_source_dir() / s
endforeach

plugin_sources += {
  'mpeghuimanager': pathsep.join(doc_sources)
}

mpeghdec_dep = dependency('mpeghdec', version : '>= 3.0.2', required : get_option('mpeghdec'), allow_fallback: false, static: false)

if mpeghdec_dep.found()
  gstmpeghuimanager = library('gstmpeghuimanager',
    mpeghuimanager_sources,
    c_args : gst_plugins_bad_args,
    include_directories : [configinc],
    dependencies : [gstaudio_dep, gstpbutils_dep, mpeghdec_dep],
    install : true,
    install_dir : plugins_install_dir,
  )
  plugins += [gstmpeghuimanager]
endif
