opengl_sources = [
  'gstopengl.c',
  'gstglalpha.c',
  'gstgluploadelement.c',
  'gstgldownloadelement.c',
  'gstglcolorconvertelement.c',
  'gstglfilterbin.c',
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
  'gstglbasemixer.c',
  'gstglcolorscale.c',
  'gstglcolorbalance.c',
  'gstgldeinterlace.c',
  'gstglfiltershader.c',
  'gstglfilterapp.c',
  'gstglmixerbin.c',
  'gstglmixer.c',
  'gstglvideomixer.c',
  'gstglstereomix.c',
  'gstglstereosplit.c',
  'gstglviewconvert.c',
  'gltestsrc.c',
  'gstgltestsrc.c',
  'gstgloverlaycompositorelement.c',
  'gstglutils.c'
]

if get_option('gl').disabled() or not gstgl_dep.found()
  subdir_done()
endif

optional_deps = []

if gl_dep.found() # have desktop GL
  opengl_sources += [
    'gstglfilterglass.c',
    'gstglmosaic.c',
  ]
endif

graphene_dep = dependency('graphene-1.0', version : '>=1.4.0', required : get_option('gl-graphene'))
if graphene_dep.found()
  optional_deps += graphene_dep
  core_conf.set('HAVE_GRAPHENE', true)
  opengl_sources += [
    'gstgltransformation.c',
    'gstglvideoflip.c',
  ]
endif

png_dep = dependency('libpng', version : '>=1.0', required : get_option('gl-png'))
jpeg_dep = dependency('libjpeg', required : get_option('gl-jpeg'))

if png_dep.found()
  optional_deps += png_dep
  core_conf.set('HAVE_PNG', true)
  opengl_sources += [
    'gstgldifferencematte.c',
  ]
  if jpeg_dep.found()
    optional_deps += jpeg_dep
    core_conf.set('HAVE_JPEG', true)
    opengl_sources += [
      'gstgloverlay.c',
    ]
  endif
endif

if glconf.get('GST_GL_HAVE_WINDOW_COCOA', 0) == 1
  foundation_dep = dependency('appleframeworks', modules : ['Foundation'], required : false)
  quartzcore_dep = dependency('appleframeworks', modules : ['QuartzCore'], required : false)
  if foundation_dep.found() and quartzcore_dep.found() # have cocoa
    opengl_sources += [
      'caopengllayersink.m',
    ]
    optional_deps += quartzcore_dep
  endif
endif

if x11_dep.found()
  # for XInitThreads()
  optional_deps += x11_dep
endif

if bcm_host_dep.found()
  optional_deps += bcm_host_dep
endif

if egl_dep.found() and cc.has_header('libdrm/drm_fourcc.h')
  optional_deps += allocators_dep
endif

gstopengl = library('gstopengl',
  opengl_sources,
  c_args : gst_plugins_base_args,
  link_args : noseh_link_args,
  include_directories : [configinc],
  dependencies : [gstgl_dep, video_dep,
                  gst_base_dep, gst_controller_dep, libm] + optional_deps,
  install : true,
  install_dir : plugins_install_dir)

pkgconfig.generate(gstopengl, install_dir : plugins_pkgconfig_install_dir)
