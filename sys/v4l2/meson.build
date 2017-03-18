v4l2_sources = [
  'gstv4l2.c',
  'gstv4l2allocator.c',
  'gstv4l2colorbalance.c',
  'gstv4l2deviceprovider.c',
  'gstv4l2object.c',
  'gstv4l2bufferpool.c',
  'gstv4l2sink.c',
  'gstv4l2src.c',
  'gstv4l2radio.c',
  'gstv4l2tuner.c',
  'gstv4l2transform.c',
  'gstv4l2videodec.c',
  'gstv4l2vidorient.c',
  'v4l2_calls.c',
  'v4l2-utils.c',
  'tuner.c',
  'tunerchannel.c',
  'tunernorm.c'
]

cdata.set('GST_V4L2_ENABLE_PROBE', get_option('v4l2-probe'))

if cc.has_header('linux/videodev2.h') or cc.has_header('sys/videodev2.h') or cc.has_header('sys/videoio.h')
  message('building v4l2 plugin')
  cdata.set('HAVE_GST_V4L2', true)
  gudev_dep = dependency('gudev-1.0', version : '>=147', required : false)
  cdata.set('HAVE_GUDEV', gudev_dep.found())

  # libv4l2 is only needed for converting some obscure formats
  # FIXME: Add a full list of the formats here
  if get_option('with-libv4l2')
    libv4l2_dep = dependency('libv4l2', required : false)
    cdata.set('HAVE_LIBV4L2', libv4l2_dep.found())
    libv4l2_deps = [libv4l2_dep]
  else
    libv4l2_deps = []
  endif

  gstv4l2 = library('gstvideo4linux2',
    v4l2_sources,
    c_args : gst_plugins_good_args,
    include_directories : [configinc, libsinc],
    dependencies : [gstbase_dep, gstvideo_dep, gstallocators_dep, gudev_dep] + libv4l2_deps,
    install : true,
    install_dir : plugins_install_dir,
  )
endif
