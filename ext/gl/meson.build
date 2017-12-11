opengl_sources = [
  'gstopengl.c',
  'gstglbasemixer.c',
  'gstglmixerbin.c',
  'gstglmixer.c',
  'gstglvideomixer.c',
  'gstglstereomix.c',
]

if build_gstgl and gstgl_dep.found()
  optional_deps = []
  opengl_defines = []

  if gl_dep.found() # have desktop GL
    opengl_sources += [
      'gstglmosaic.c',
    ]
  endif

  if x11_dep.found()
    # for XInitThreads()
    optional_deps += x11_dep
  endif

  if bcm_host_dep.found()
    optional_deps += bcm_host_dep
  endif

  if egl_dep.found() and cc.has_header('libdrm/drm_fourcc.h', required : false)
    optional_deps += gstallocators_dep
  endif

  gstopenglmixers = library('gstopenglmixers',
    opengl_sources,
    c_args : gst_plugins_bad_args + opengl_defines,
    link_args : noseh_link_args,
    include_directories : [configinc],
    dependencies : [gstbadvideo_dep, gstgl_dep, gstvideo_dep,
                    gstbase_dep, gstcontroller_dep, libm] + optional_deps,
    install : true,
    install_dir : plugins_install_dir,
  )
endif
