# mjpegtools upstream breaks API constantly and doesn't export the version in
# a header anywhere. The configure file has a lot of logic to support old
# versions, but it all seems untested and broken. Require 2.0.0. Can be changed
# if someone complains.
mjpegtools_dep = dependency('mjpegtools', version : '>=2.0.0', required : get_option('mpeg2enc'))
mpeg2enc_dep = cxx.find_library('mpeg2encpp', required : get_option('mpeg2enc'))
if mjpegtools_dep.found() and mpeg2enc_dep.found()
  gstmpeg2enc = library('gstmpeg2enc',
    'gstmpeg2enc.cc',
    'gstmpeg2encoptions.cc',
    'gstmpeg2encoder.cc',
    'gstmpeg2encstreamwriter.cc',
    'gstmpeg2encpicturereader.cc',
    cpp_args : gst_plugins_bad_args + ['-DGST_MJPEGTOOLS_API=20000'],
    link_args : noseh_link_args,
    include_directories : [configinc, libsinc],
    dependencies : [gstvideo_dep, mjpegtools_dep, mpeg2enc_dep],
    install : true,
    install_dir : plugins_install_dir,
  )
  pkgconfig.generate(gstmpeg2enc, install_dir : plugins_pkgconfig_install_dir)
endif
