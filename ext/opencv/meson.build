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
  'gstdewarp.cpp'
]

libopencv2_headers = [
  'opencv2/core/core_c.h',
  'opencv2/core/version.hpp',
  'opencv2/highgui/highgui_c.h',
  'opencv2/imgproc/imgproc.hpp',
  'opencv2/imgproc/imgproc_c.h',
  'opencv2/objdetect/objdetect.hpp',
  'opencv2/video/background_segm.hpp',
]

libopencv3_headers = [
  'opencv2/bgsegm.hpp',
]

gstopencv_cargs = ['-DGST_HAAR_CASCADES_DIR="@0@"']

# First, check for the upper version limit and ensure it isn't found
# FIXME: When 0.37.0 is released, change this to use many-version-conditions
opencv3_1_dep = dependency('opencv', version : '>3.1.0', required : false)
# Then, check if the lower version limit is found
opencv2_dep = dependency('opencv', version : '>=2.3.0', required : false)
opencv3_dep = dependency('opencv', version : '>= 3.0 ', required : false)

opencv2_found = false
if opencv3_1_dep.found()
  message('OpenCV version is too new: \'' + opencv3_1_dep.version() + '\' (need <= 3.1.0)')
elif opencv2_dep.found()
  message('OpenCV found, version is \'' + opencv2_dep.version() + '\'')
  opencv2_found = true
  foreach h : libopencv2_headers
    if not cxx.has_header(h)
      message('Needed header "' + h + '" not found')
      opencv2_found = false
    endif
  endforeach

  if opencv3_dep.found()
    foreach h : libopencv3_headers
      if not cxx.has_header(h)
        message('Needed header "' + h + '" not found')
        opencv2_found = false
      endif
    endforeach
  endif
endif

if opencv2_found
  opencv2_prefix = opencv2_dep.get_pkgconfig_variable('prefix')
  gstopencv_cargs += ['-DOPENCV_PREFIX="' + opencv2_prefix + '"']

  # Check the data dir used by opencv for its xml data files
  # Use prefix from pkg-config to be compatible with cross-compilation
  r = run_command('test', '-d', opencv2_prefix + '/share/opencv')
  if r.returncode() == 0
    gstopencv_cargs += '-DOPENCV_PATH_NAME="opencv"'
  else
    r = run_command('test', '-d', opencv2_prefix + '/share/OpenCV')
    if r.returncode() == 0
      gstopencv_cargs += '-DOPENCV_PATH_NAME="OpenCv"'
    else
      error('Unable to detect OpenCV data directory')
    endif
  endif
  gstopencv = library('gstopencv',
    gstopencv_sources,
    cpp_args : gst_plugins_bad_args + gstopencv_cargs + [ '-DGST_USE_UNSTABLE_API' ],
    link_args : noseh_link_args,
    include_directories : [configinc],
    dependencies : [gstbase_dep, gstvideo_dep, opencv2_dep, gstopencv_dep],
    install : true,
    install_dir : plugins_install_dir,
  )
endif
