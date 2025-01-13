v4l2_sources = [
  'gstv4l2.c',
  'gstv4l2element.c',
  'gstv4l2allocator.c',
  'gstv4l2codec.c',
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
  'gstv4l2videoenc.c',
  'gstv4l2fwhtenc.c',
  'gstv4l2h263enc.c',
  'gstv4l2h264codec.c',
  'gstv4l2h264enc.c',
  'gstv4l2h265codec.c',
  'gstv4l2h265enc.c',
  'gstv4l2jpegenc.c',
  'gstv4l2mpeg2codec.c',
  'gstv4l2mpeg4codec.c',
  'gstv4l2mpeg4enc.c',
  'gstv4l2vidorient.c',
  'gstv4l2vp8codec.c',
  'gstv4l2vp8enc.c',
  'gstv4l2vp9codec.c',
  'gstv4l2vp9enc.c',
  'v4l2_calls.c',
  'v4l2-utils.c',
  'tuner.c',
  'tunerchannel.c',
  'tunernorm.c'
]

v4l2_headers = [
  'tunernorm.h',
  'gstv4l2vp8enc.h',
  'gstv4l2colorbalance.h',
  'gstv4l2transform.h',
  'gstv4l2videodec.h',
  'gstv4l2mpeg2codec.h',
  'gstv4l2vp8codec.h',
  'gstv4l2vp9codec.h',
  'gstv4l2h264enc.h',
  'gstv4l2codec.h',
  'gstv4l2vp9enc.h',
  'gstv4l2fwhtenc.h',
  'gstv4l2vidorient.h',
  'gstv4l2h263enc.h',
  'gstv4l2deviceprovider.h',
  'gstv4l2bufferpool.h',
  'gstv4l2tuner.h',
  'gstv4l2elements.h',
  'gstv4l2radio.h',
  'gstv4l2h265codec.h',
  'gstv4l2sink.h',
  'gstv4l2h265enc.h',
  'v4l2-utils.h',
  'gstv4l2mpeg4enc.h',
  'tunerchannel.h',
  'gstv4l2videoenc.h',
  'gstv4l2h264codec.h',
  'gstv4l2src.h',
  'tuner.h',
  'gstv4l2mpeg4codec.h',
  'gstv4l2object.h',
  'gstv4l2jpegenc.h',
  'gstv4l2allocator.h',
]

doc_sources = []
foreach s: v4l2_sources + v4l2_headers
  doc_sources += meson.current_source_dir() / s
endforeach

plugin_sources += {
  'video4linux2': pathsep.join(doc_sources)
}

v4l2 = get_option('v4l2')
if v4l2.disabled()
  have_v4l2 = false
  message('V4L2 plugin is disabled')
else
  have_v4l2 = cc.has_header('linux/videodev2.h') or cc.has_header('sys/videodev2.h') or cc.has_header('sys/videoio.h')
  if v4l2.enabled() and not have_v4l2
    error('V4L2 is requested but headers were not found')
  endif
endif

cdata.set('GST_V4L2_ENABLE_PROBE', get_option('v4l2-probe'))

if have_v4l2
  message('building v4l2 plugin')
  cdata.set('HAVE_GST_V4L2', true)
  gudev_dep = dependency('gudev-1.0', version : '>=147', required : get_option('v4l2-gudev'), allow_fallback: true)
  cdata.set('HAVE_GUDEV', gudev_dep.found())

  # libv4l2 is only needed for converting some obscure formats
  # FIXME: Add a full list of the formats here
  libv4l2_dep = dependency('libv4l2', required : get_option('v4l2-libv4l2'))
  cdata.set('HAVE_LIBV4L2', libv4l2_dep.found())

  gstv4l2 = library('gstvideo4linux2',
    v4l2_sources,
    c_args : gst_plugins_good_args,
    include_directories : [configinc, libsinc],
    dependencies : [gstbase_dep, gstvideo_dep, gstallocators_dep, gudev_dep, libv4l2_dep],
    install : true,
    install_dir : plugins_install_dir,
  )
  plugins += [gstv4l2]
endif
