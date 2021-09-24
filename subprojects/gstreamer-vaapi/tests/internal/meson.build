libdecutils_sources = [
  'decoder.c',
  'test-h264.c',
  'test-jpeg.c',
  'test-mpeg2.c',
  'test-mpeg4.c',
  'test-vc1.c',
]

libdecutils_headers = [
  'decoder.h',
  'test-h264.h',
  'test-jpeg.h',
  'test-mpeg2.h',
  'test-mpeg4.h',
  'test-vc1.h',
]

libutils_sources = [
  'codec.c',
  'image.c',
  'output.c',
  'test-subpicture-data.c',
  'y4mreader.c',
]

libutils_headers = [
  'codec.h',
  'image.h',
  'output.h',
  'test-subpicture-data.h',
  'y4mreader.h',
]

test_examples = [
  'simple-decoder',
  'test-decode',
  'test-display',
  'test-filter',
  'test-surfaces',
  'test-windows',
  'test-subpicture',
]

if USE_ENCODERS
  test_examples += [ 'simple-encoder' ]
endif

if USE_GLX
  test_examples += [ 'test-textures' ]
endif

libutils = static_library('libutils',
  libutils_sources + libutils_headers,
  c_args : gstreamer_vaapi_args,
  include_directories: [configinc, libsinc],
  dependencies : gstlibvaapi_deps,
  install: false)

libdecutils = static_library('libdecutils',
  libdecutils_sources + libdecutils_headers,
  c_args : gstreamer_vaapi_args,
  include_directories: [configinc, libsinc],
  dependencies : gstlibvaapi_deps,
  install: false)

foreach example : test_examples
  executable(example, '@0@.c'.format(example),
    c_args : gstreamer_vaapi_args,
    include_directories: [configinc, libsinc],
    dependencies : [gst_dep, libva_dep,  gstlibvaapi_dep],
    link_with: [libutils, libdecutils],
    install: false)
endforeach
