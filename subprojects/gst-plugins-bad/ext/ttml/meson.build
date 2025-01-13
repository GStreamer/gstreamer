libxml_dep = dependency('libxml-2.0', version : '>= 2.9.2', required : get_option('ttml'))
pango_dep = dependency('pango', required : get_option('ttml'))
cairo_dep = dependency('cairo', required : get_option('ttml'))
pangocairo_dep = dependency('pangocairo', required : get_option('ttml'))

ttmlsubs_sources = [
  'subtitle.c',
   'subtitlemeta.c',
   'gstttmlparse.c',
   'ttmlparse.c',
   'gstttmlrender.c',
   'gstttmlelement.c',
   'gstttmlplugin.c',
]

ttmlsubs_headers = [
  'gstttmlelements.h',
  'subtitlemeta.h',
  'gstttmlparse.h',
  'subtitle.h',
  'gstttmlrender.h',
  'ttmlparse.h',
]

doc_sources = []
foreach s: ttmlsubs_sources + ttmlsubs_headers
  doc_sources += meson.current_source_dir() / s
endforeach

plugin_sources += {
  'ttmlsubs': pathsep.join(doc_sources)
}

if libxml_dep.found() and pango_dep.found() and cairo_dep.found() and pangocairo_dep.found()
  gstttmlsubs = library('gstttmlsubs',
    ttmlsubs_sources,
    c_args : gst_plugins_bad_args + cc.get_supported_arguments(['-Wno-aggregate-return']),
    include_directories : [configinc],
    dependencies : [gstvideo_dep, libxml_dep, pango_dep, cairo_dep, pangocairo_dep, libm],
    install : true,
    install_dir : plugins_install_dir,
  )
  plugins += [gstttmlsubs]
endif
