opencv_sources = [
  'gstopencvutils.cpp',
  'gstopencvvideofilter.cpp',
]

opencv_headers = [
  'gstopencvutils.h',
  'gstopencvvideofilter.h',
]
install_headers(opencv_headers, subdir : 'gstreamer-1.0/gst/opencv')

opencv_dep = dependency('opencv', version : '>= 2.3.0', required : false)
if opencv_dep.found()
  gstopencv = library('gstopencv-' + api_version,
    opencv_sources,
    c_args : gst_plugins_bad_args,
    include_directories : [configinc, libsinc],
    version : libversion,
    soversion : soversion,
    install : true,
    dependencies : [gstbase_dep, gstvideo_dep, opencv_dep],
  )

  gstopencv_dep = declare_dependency(link_with: gstopencv,
    include_directories : [libsinc],
    dependencies : [gstvideo_dep, opencv_dep])
endif
