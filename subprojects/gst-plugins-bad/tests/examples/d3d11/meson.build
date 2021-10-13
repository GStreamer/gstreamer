if host_system == 'windows'
  executable('d3d11videosink',
    ['d3d11videosink.c', 'd3d11videosink-kb.c'],
    c_args : gst_plugins_bad_args,
    include_directories : [configinc, libsinc],
    dependencies: [gst_dep, gstbase_dep, gstvideo_dep],
    install: false,
  )

  executable('d3d11screencapturesrc',
    ['d3d11screencapturesrc.cpp'],
    c_args : gst_plugins_bad_args,
    include_directories : [configinc, libsinc],
    dependencies: [gst_dep, gstbase_dep, gstvideo_dep],
    install: false,
  )

  d3d11_lib = cc.find_library('d3d11', required : false)
  dxgi_lib = cc.find_library('dxgi', required : false)
  d3dcompiler_lib = cc.find_library('d3dcompiler', required: false)
  have_d3d11_h = cc.has_header('d3d11.h')
  have_dxgi_h = cc.has_header('dxgi1_2.h')
  have_d3d11compiler_h = cc.has_header('d3dcompiler.h')

  d3d9_dep = cc.find_library('d3d9', required : false)
  have_d3d9_h = cc.has_header('d3d9.h')

  if d3d11_lib.found() and dxgi_lib.found() and d3dcompiler_lib.found() and have_d3d11_h and have_dxgi_h and have_d3d11compiler_h
    executable('d3d11videosink-shared-texture',
      ['d3d11videosink-shared-texture.cpp', 'd3d11device.cpp'],
      c_args : gst_plugins_bad_args,
      include_directories : [configinc, libsinc],
      dependencies: [gst_dep, gstbase_dep, gstvideo_dep, d3d11_lib, dxgi_lib, d3dcompiler_lib],
      install: false,
    )

    if d3d_dep.found() and have_d3d9_h
      executable('d3d11videosink-shared-texture-d3d9ex',
        ['d3d11videosink-shared-texture-d3d9ex.cpp', 'd3d11device.cpp'],
        c_args : gst_plugins_bad_args,
        include_directories : [configinc, libsinc],
        dependencies: [gst_dep, gstbase_dep, gstvideo_dep, d3d11_lib, dxgi_lib, d3dcompiler_lib, d3d9_dep],
        install: false,
      )
    endif
  endif
endif
