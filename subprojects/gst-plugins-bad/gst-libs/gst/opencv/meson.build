opencv_opt = get_option('opencv')
if opencv_opt.disabled()
  opencv_dep = disabler()
  subdir_done()
endif

opencv_sources = files([
  'gstopencvutils.cpp',
  'gstopencvvideofilter.cpp',
])

opencv_headers = files([
  'opencv-prelude.h',
  'gstopencvutils.h',
  'gstopencvvideofilter.h',
])

libopencv_headers = [
  'opencv2/bgsegm.hpp',
  'opencv2/calib3d.hpp',
  'opencv2/core.hpp',
  'opencv2/imgcodecs.hpp',
  'opencv2/imgproc.hpp',
  'opencv2/objdetect.hpp',
  'opencv2/opencv.hpp',
  'opencv2/video.hpp',
  'opencv2/tracking.hpp',
]


gstopencv_cargs = []
opencv_dep = dependency('opencv', version : ['>= 3.0.0', '< 3.5.0'], required : false)
if not opencv_dep.found()
  opencv_dep = dependency('opencv', version : ['>= 4.0.0'], required : opencv_opt)
  if not opencv_dep.found()
    subdir_done()
  endif
endif

opencv_found = true
foreach h : libopencv_headers
  if not cxx.has_header(h, dependencies: opencv_dep)
    message('Needed header "' + h + '" not found')
    opencv_found = false
  endif
endforeach

if not opencv_found
  if opencv_opt.enabled()
    error('opencv enabled, but needed headers not found')
  endif
  subdir_done()
endif

opencv_prefix = opencv_dep.get_variable('prefix')
gstopencv_cargs += ['-DOPENCV_PREFIX="' + opencv_prefix + '"']
# opencv4 seems to ship with .pc file that references non-existent include dir
# (/usr/include/opencv4/opencv instead of /usr/include/opencv4/opencv2)
# clang 10 complains about the following header in opencv4
# /usr/include/opencv4/opencv2/flann/logger.h:83:36: error: format string is not a string literal [-Werror,-Wformat-nonliteral]
gstopencv_cargs += cxx.get_supported_arguments(['-Wno-missing-include-dirs', '-Wno-format-nonliteral'])

# Check the data dir used by opencv for its xml data files
# Use prefix from pkg-config to be compatible with cross-compilation
fsmod = import('fs')
opencv_path_name = ''
foreach d : ['opencv', 'OpenCV', 'opencv4']
  if fsmod.is_dir(opencv_prefix / 'share' / d)
    opencv_path_name = d
    break
  endif
endforeach
if opencv_path_name != ''
  gstopencv_cargs += [f'-DOPENCV_PATH_NAME="@d@"']
else
  if opencv_opt.enabled()
    error('opencv enabled, but data directory not found')
  endif
  message('Unable to detect OpenCV data directory')
  subdir_done()
endif

pkg_name = 'gstreamer-opencv-1.0'
gstopencv = library('gstopencv-' + api_version,
  opencv_sources,
  c_args : gst_plugins_bad_args + ['-DBUILDING_GST_OPENCV'],
  cpp_args : gst_plugins_bad_args + gstopencv_cargs + ['-DBUILDING_GST_OPENCV', '-DG_LOG_DOMAIN="GStreamer-OpenCV"'],
  override_options : ['cpp_std=c++11'],
  include_directories : [configinc, libsinc],
  version : libversion,
  soversion : soversion,
  darwin_versions : osxversion,
  install : true,
  dependencies : [gstbase_dep, gstvideo_dep, opencv_dep],
)

doc_sources = []
foreach s: opencv_sources + opencv_headers
  doc_sources += s.full_path()
endforeach

libs_c_sources += {
  'opencv': pathsep.join(doc_sources)
}

gst_libraries += [[pkg_name, {'lib': gstopencv}]]
gstopencv_dep = declare_dependency(link_with: gstopencv,
  include_directories : [libsinc],
  dependencies : [gstvideo_dep, opencv_dep])
meson.override_dependency(pkg_name, gstopencv_dep)

install_headers(opencv_headers, subdir : 'gstreamer-1.0/gst/opencv')
