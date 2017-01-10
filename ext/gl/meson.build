opengl_sources = [
  'gstopengl.c',
  'gstglbasemixer.c',
  'gstgluploadelement.c',
  'gstgldownloadelement.c',
  'gstglcolorconvertelement.c',
  'gstglfilterbin.c',
  'gstglmixerbin.c',
  'gstglsinkbin.c',
  'gstglsrcbin.c',
  'gstglimagesink.c',
  'gstglfiltercube.c',
  'gstgleffects.c',
  'effects/gstgleffectscurves.c',
  'effects/gstgleffectssources.c',
  'effects/gstgleffectidentity.c',
  'effects/gstgleffectmirror.c',
  'effects/gstgleffectsqueeze.c',
  'effects/gstgleffectstretch.c',
  'effects/gstgleffectfisheye.c',
  'effects/gstgleffecttwirl.c',
  'effects/gstgleffectbulge.c',
  'effects/gstgleffecttunnel.c',
  'effects/gstgleffectsquare.c',
  'effects/gstgleffectlumatocurve.c',
  'effects/gstgleffectrgbtocurve.c',
  'effects/gstgleffectsin.c',
  'effects/gstgleffectxray.c',
  'effects/gstgleffectglow.c',
  'effects/gstgleffectblur.c',
  'effects/gstgleffectsobel.c',
  'effects/gstgleffectlaplacian.c',
  'gstglcolorscale.c',
  'gstglcolorbalance.c',
  'gstglmixer.c',
  'gstglvideomixer.c',
  'gstglfiltershader.c',
  'gstglfilterapp.c',
  'gstglviewconvert.c',
  'gstglstereosplit.c',
  'gstgldeinterlace.c',
  'gstglstereomix.c',
  'gltestsrc.c',
  'gstgltestsrc.c',
  'gstglutils.c'
]

if build_gstgl and gstgl_dep.found()
  optional_deps = []
  opengl_defines = ['-DGST_USE_UNSTABLE_API']

  if gl_dep.found() # have desktop GL
    opengl_sources += [
      'gstglfilterglass.c',
      'gstglmosaic.c',
    ]
  endif

  graphene_dep = dependency('graphene-1.0', version : '>=1.4.0', required : false)
  if graphene_dep.found()
    optional_deps += graphene_dep
    opengl_defines += '-DHAVE_GRAPHENE=1'
    opengl_sources += [
      'gstgltransformation.c',
      'gstglvideoflip.c',
    ]
  endif

  png_dep = dependency('libpng', version : '>=1.0', required : false)
  jpeg_dep = cc.find_library('jpeg-mmx', required : false)
  if not jpeg_dep.found()
    jpeg_dep = cc.find_library('jpeg', required : false)
  endif

  if png_dep.found()
    optional_deps += png_dep
    opengl_defines += '-DHAVE_PNG=1'
    opengl_sources += [
      'gstgldifferencematte.c',
    ]
    if jpeg_dep.found()
      optional_deps += jpeg_dep
      opengl_defines += '-DHAVE_JPEG=1'
      opengl_sources += [
        'gstgloverlay.c',
      ]
    endif
  endif

  if false # have cocoa
    opengl_sources += [
      'caopengllayersink.m',
    ]
  endif

  if x11_dep.found()
    # for XInitThreads()
    optional_deps += x11_dep
  endif

  if bcm_host_dep.found()
    optional_deps += bcm_host_dep
  endif

  gstopengl = library('gstopengl',
    opengl_sources,
    c_args : gst_plugins_bad_args + opengl_defines,
    link_args : noseh_link_args,
    include_directories : [configinc],
    dependencies : [gstgl_dep, gstbadvideo_dep, gstbadbase_dep, gstvideo_dep,
                    gstbase_dep, gstcontroller_dep, libm] + optional_deps,
    install : true,
    install_dir : plugins_install_dir,
  )
endif
