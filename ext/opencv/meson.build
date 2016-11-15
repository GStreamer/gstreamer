opencv_sources = [
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
  'motioncells_wrapper.cpp'
]

opencv_cargs = ['-DGST_HAAR_CASCADES_DIR="@0@"']
runcmd = run_command('pkg-config', '--variable=prefix', 'opencv')
if runcmd.returncode() == 0
  opencv_cargs += '-DOPENCV_PREFIX="' + runcmd.stdout().strip() + '"'
endif

r = run_command('test', '-d', '/usr/share/opencv')
if r.returncode() == 0
  opencv_cargs += '-DOPENCV_PATH_NAME="opencv"'
else
  opencv_cargs += '-DOPENCV_PATH_NAME="OpenCv"'
endif

opencv_dep = dependency('opencv', version : '>= 2.3.0')
if opencv_dep.found()
  gstopencv = library('gstopencv',
    opencv_sources,
    cpp_args : gst_plugins_bad_args + opencv_cargs + [ '-DGST_USE_UNSTABLE_API' ],
    link_args : noseh_link_args,
    include_directories : [configinc],
    dependencies : [gstbase_dep, gstvideo_dep, opencv_dep, gstopencv_dep],
    install : true,
    install_dir : plugins_install_dir,
  )
endif
