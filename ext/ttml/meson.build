libxml_dep = dependency('libxml-2.0', version : '>= 2.9.2', required : false)
pango_dep = dependency('pango', required : false)
cairo_dep = dependency('cairo', required : false)
pangocairo_dep = dependency('pangocairo', required : false)
cc = meson.get_compiler('c')
m_dep = cc.find_library('m', required : false)

if libxml_dep.found() and pango_dep.found() and cairo_dep.found() and pangocairo_dep.found()
  gstttmlsubs = library('gstttmlsubs',
    ['subtitle.c',
     'subtitlemeta.c',
     'gstttmlparse.c',
     'ttmlparse.c',
     'gstttmlrender.c',
     'gstttmlplugin.c'],
    c_args : gst_plugins_bad_args,
    include_directories : [configinc],
    dependencies : [gstvideo_dep, libxml_dep, pango_dep, cairo_dep, pangocairo_dep, m_dep],
    install : true,
    install_dir : plugins_install_dir,
  )
endif
