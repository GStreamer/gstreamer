jpeg_sources = [
  'gstjpeg.c',
  'gstjpegenc.c',
  'gstjpegdec.c',
]

jpeglib = cc.find_library('jpeg', required : false)

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
endif
