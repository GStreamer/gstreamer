png_sources = [
  'gstpng.c',
  'gstpngenc.c',
  'gstpngdec.c',
]

png_headers = [
  'gstpngenc.h',
  'gstpng.h',
  'gstpngdec.h',
]

doc_sources = []
foreach s: png_sources + png_headers
  doc_sources += meson.current_source_dir() / s
endforeach

plugin_sources += {
  'png': pathsep.join(doc_sources)
}

libpng_dep = dependency('libpng', version : '>=1.2', required : get_option('png'))

if libpng_dep.found()
  gstpng = library('gstpng',
    png_sources,
    c_args : gst_plugins_good_args,
    link_args : noseh_link_args,
    include_directories : [configinc, libsinc],
    dependencies : [gstbase_dep, gstvideo_dep, libpng_dep],
    install : true,
    install_dir : plugins_install_dir,
  )
  plugins += [gstpng]
endif
