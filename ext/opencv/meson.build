if get_option('opencv').disabled()
  subdir_done()
endif

gstopencv_sources = [
  'gstcvdilate.cpp',
  'gstcvdilateerode.cpp',
  'gstcvequalizehist.cpp',
  'gstcverode.cpp',
  'gstcvlaplace.cpp',
  'gstcvsmooth.cpp',
  'gstcvsobel.cpp',
  'gstdisparity.cpp',
  'gstedgedetect.cpp',
  'gstfaceblur.cpp',
  'gstfacedetect.cpp',
  'gstgrabcut.cpp',
  'gsthanddetect.cpp',
  'gstmotioncells.cpp',
  'gstopencv.cpp',
  'gstretinex.cpp',
  'gstsegmentation.cpp',
  'gstskindetect.cpp',
  'gsttemplatematch.cpp',
  'gsttextoverlay.cpp',
  'MotionCells.cpp',
  'motioncells_wrapper.cpp',
  'gstdewarp.cpp',
  'camerautils.cpp',
  'cameraevent.cpp',
  'gstcameracalibrate.cpp',
  'gstcameraundistort.cpp'
]

libopencv_headers = [
  'opencv2/bgsegm.hpp',
  'opencv2/calib3d.hpp',
  'opencv2/core.hpp',
  'opencv2/imgproc.hpp',
  'opencv2/objdetect.hpp',
  'opencv2/opencv.hpp',
  'opencv2/video.hpp',
]

libopencv4_headers = [
  'opencv4/opencv2/bgsegm.hpp',
  'opencv4/opencv2/calib3d.hpp',
  'opencv4/opencv2/core.hpp',
  'opencv4/opencv2/imgproc.hpp',
  'opencv4/opencv2/objdetect.hpp',
  'opencv4/opencv2/opencv.hpp',
  'opencv4/opencv2/video.hpp',
]

gstopencv_cargs = ['-DGST_HAAR_CASCADES_DIR="@0@"']

opencv_dep = dependency('opencv', version : ['>= 3.0.0', '< 3.5.0'], required : false)
opencv_found = opencv_dep.found()

if opencv_found
  foreach h : libopencv_headers
    if not cxx.has_header(h)
      message('Needed header "' + h + '" not found')
      opencv_found = false
    endif
  endforeach
else
  opencv_dep = dependency('opencv4', version : ['>= 4.0.0', '< 4.1.0'], required : false)
  opencv_found = opencv_dep.found()
  if opencv_found
    foreach h : libopencv4_headers
      if not cxx.has_header(h)
        message('Needed header "' + h + '" not found')
        opencv_found = false
      endif
    endforeach
  endif
endif

if opencv_found
  opencv_prefix = opencv_dep.get_pkgconfig_variable('prefix')
  gstopencv_cargs += ['-DOPENCV_PREFIX="' + opencv_prefix + '"']

  # Check the data dir used by opencv for its xml data files
  # Use prefix from pkg-config to be compatible with cross-compilation
  r = run_command('test', '-d', opencv_prefix + '/share/opencv')
  if r.returncode() == 0
    gstopencv_cargs += '-DOPENCV_PATH_NAME="opencv"'
  else
    r = run_command('test', '-d', opencv_prefix + '/share/OpenCV')
    if r.returncode() == 0
      gstopencv_cargs += '-DOPENCV_PATH_NAME="OpenCV"'
    else
      r = run_command('test', '-d', opencv_prefix + '/share/opencv4')
      if r.returncode() == 0
        gstopencv_cargs += '-DOPENCV_PATH_NAME="opencv4"'
      else
        error('Unable to detect OpenCV data directory')
      endif
    endif
  endif
  gstopencv = library('gstopencv',
    gstopencv_sources,
    cpp_args : gst_plugins_bad_args + gstopencv_cargs + [ '-DGST_USE_UNSTABLE_API' ],
    link_args : noseh_link_args,
    include_directories : [configinc, libsinc],
    dependencies : [gstbase_dep, gstvideo_dep, opencv_dep, gstopencv_dep],
    install : true,
    install_dir : plugins_install_dir,
  )
  pkgconfig.generate(gstopencv, install_dir : plugins_pkgconfig_install_dir)
elif get_option('opencv').enabled()
  error('OpenCV support enabled but required dependencies were not found.')
endif
