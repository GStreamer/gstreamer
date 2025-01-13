lcms2_dep = dependency('lcms2', version : '>= 2.7', required : get_option('colormanagement'))

colormanagement_sources = [
  'gstcolormanagement.c',
  'gstlcms.c',
]

colormanagement_headers = [
  'gstlcms.h',
]

doc_sources = []
foreach s: colormanagement_sources + colormanagement_headers
  doc_sources += meson.current_source_dir() / s
endforeach

plugin_sources += {
  'colormanagement': pathsep.join(doc_sources)
}

if lcms2_dep.found()
  gstcolormanagement = library('gstcolormanagement',
    colormanagement_sources,
    c_args : gst_plugins_bad_args,
    link_args : noseh_link_args,
    include_directories : [configinc],
    dependencies : [gstbase_dep, gstvideo_dep, lcms2_dep],
    install : true,
    install_dir : plugins_install_dir,
  )
  plugins += [gstcolormanagement]
endif
