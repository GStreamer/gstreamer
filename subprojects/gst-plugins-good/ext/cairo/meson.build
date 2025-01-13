cairo_dep = dependency('cairo-gobject', version : '>=1.10.0', required : get_option('cairo'),
                       default_options: ['glib=enabled'])

cairo_sources = [
  'gstcairo.c',
  'gstcairooverlay.c',
]

cairo_headers = [
  'gstcairooverlay.h',
]

doc_sources = []
foreach s: cairo_sources + cairo_headers
  doc_sources += meson.current_source_dir() / s
endforeach

plugin_sources += {
  'cairo': pathsep.join(doc_sources)
}

if cairo_dep.found()
  gstcairo = library('gstcairo',
    cairo_sources,
    c_args : gst_plugins_good_args,
    link_args : noseh_link_args,
    include_directories : [configinc],
    dependencies : [gstbase_dep, gstvideo_dep, cairo_dep],
    install : true,
    install_dir : plugins_install_dir,
  )
  plugins += [gstcairo]
endif
