if omx_target == 'rpi'
  egl_sources = ['testegl.c']

  egl_dep = dependency('egl', required : false)
  if not egl_dep.found()
    egl_dep = cc.find_library ('EGL')
  endif

  gles2_dep = dependency('glesv2', required : false)
  if not gles2_dep.found()
    gles2_dep = cc.find_library ('GLESv2')
  endif

  executable ('testegl',
      sources : egl_sources,
      c_args : gst_omx_args,
      include_directories : [configinc],
      dependencies : [libm, gst_dep, gstvideo_dep, gstgl_dep, egl_dep, gles2_dep])
endif
