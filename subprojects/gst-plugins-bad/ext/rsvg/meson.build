rsvg_sources = [
  'gstrsvg.c',
  'gstrsvgdec.c',
  'gstrsvgoverlay.c',
]

cairo_dep = dependency('cairo', version: '>= 1.16.0', allow_fallback: true, required : get_option('rsvg'))
rsvg_dep = dependency('librsvg-2.0', version : '>= 2.36.2', required : get_option('rsvg'))
if cairo_dep.found() and rsvg_dep.found()
  gstrsvg = library('gstrsvg',
    rsvg_sources,
    c_args : gst_plugins_bad_args,
    link_args : noseh_link_args,
    include_directories : [configinc],
    dependencies : [gstvideo_dep, cairo_dep, rsvg_dep],
    install : true,
    install_dir : plugins_install_dir,
  )
  plugins += [gstrsvg]
endif
