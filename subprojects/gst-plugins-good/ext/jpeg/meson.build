jpeg_sources = [
  'gstjpeg.c',
  'gstjpegplugin.c',
  'gstjpegenc.c',
  'gstjpegdec.c',
]

jpeg_headers = [
  'smokecodec.h',
  'gstjpegelements.h',
  'gstjpegenc.h',
  'gstjpegdec.h',
  'gstjpeg.h',
  'gstsmokedec.h',
  'smokeformat.h',
  'gstsmokeenc.h',
]

doc_sources = []
foreach s: jpeg_sources + jpeg_headers
  doc_sources += meson.current_source_dir() / s
endforeach

plugin_sources += {
  'jpeg': pathsep.join(doc_sources)
}

jpeglib = dependency('libjpeg', required : get_option('jpeg'),
  fallback: ['libjpeg-turbo', 'jpeg_dep'])

if jpeglib.found()
  gstjpeg = library('gstjpeg',
    jpeg_sources,
    c_args : gst_plugins_good_args,
    link_args : noseh_link_args,
    include_directories : [configinc, libsinc],
    dependencies : [gst_dep, gstbase_dep, gstvideo_dep, jpeglib, libm],
    install : true,
    install_dir : plugins_install_dir,
  )
  plugins += [gstjpeg]
endif
