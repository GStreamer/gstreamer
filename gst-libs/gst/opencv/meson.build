opencv_sources = [
  'gstopencvutils.cpp',
  'gstopencvvideofilter.cpp',
]

opencv_headers = [
  'opencv-prelude.h',
  'gstopencvutils.h',
  'gstopencvvideofilter.h',
]

opencv_dep = dependency('opencv', version : '>= 3.0.0', required : get_option('opencv'))
if not opencv_dep.found()
  opencv_dep = dependency('opencv4', version : '>= 4.0.0', required : get_option('opencv'))
endif
if opencv_dep.found()
  gstopencv = library('gstopencv-' + api_version,
    opencv_sources,
    c_args : gst_plugins_bad_args + ['-DBUILDING_GST_OPENCV'],
    include_directories : [configinc, libsinc],
    version : libversion,
    soversion : soversion,
  darwin_versions : osxversion,
    install : true,
    dependencies : [gstbase_dep, gstvideo_dep, opencv_dep],
  )

  gstopencv_dep = declare_dependency(link_with: gstopencv,
    include_directories : [libsinc],
    dependencies : [gstvideo_dep, opencv_dep])

  install_headers(opencv_headers, subdir : 'gstreamer-1.0/gst/opencv')
endif
