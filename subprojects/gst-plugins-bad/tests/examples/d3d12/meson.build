if host_system != 'windows'
  subdir_done()
endif

have_d2d_h = cc.has_header('d2d1_3.h')
have_dwrite_h = cc.has_header('dwrite.h')
have_d3d12video_h = cc.has_header('d3d12video.h')
dwrite_dep = cc.find_library('dwrite', required: false)

executable('d3d12enc-dynamic-reconfigure',
  ['d3d12enc-dynamic-reconfigure.c', '../key-handler.c'],
  include_directories : [configinc],
  dependencies: [gst_dep, gstbase_dep, gstvideo_dep],
  c_args : gst_plugins_bad_args,
  install: false
)

executable('d3d12videosink-switch',
  ['d3d12videosink-switch.cpp', '../key-handler.c'],
  include_directories : [configinc],
  dependencies: [gst_dep, gstbase_dep, gstvideo_dep],
  c_args : gst_plugins_bad_args,
  install: false
)

if gstd3d12_dep.found()
  if have_d2d_h and have_dwrite_h and have_d3d12video_h and dwrite_dep.found()
    executable('d3d12videosink-overlay', ['d3d12videosink-overlay.cpp'],
      c_args : gst_plugins_bad_args + ['-DGST_USE_UNSTABLE_API'],
      cpp_args : gst_plugins_bad_args + ['-DGST_USE_UNSTABLE_API'],
      include_directories : [configinc, libsinc],
      dependencies: [gst_dep, gstvideo_dep, gstd3d12_dep, dwrite_dep],
      install: false,
    )
  endif
endif
