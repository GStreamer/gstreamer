vaapi_sources = [
  'gstvaapi.c',
  'gstvaapidecode.c',
  'gstvaapidecodedoc.c',
  'gstvaapioverlay.c',
  'gstvaapipluginbase.c',
  'gstvaapipluginutil.c',
  'gstvaapipostproc.c',
  'gstvaapipostprocutil.c',
  'gstvaapisink.c',
  'gstvaapivideobuffer.c',
  'gstvaapivideocontext.c',
  'gstvaapivideometa.c',
  'gstvaapidecodebin.c',
  'gstvaapivideobufferpool.c',
  'gstvaapivideomemory.c',
  'gstvaapivideometa_texture.c',
  'gstvaapidecode_props.c',
]

if USE_ENCODERS
  vaapi_sources += [
      'gstvaapiencode.c',
      'gstvaapiencode_h264.c',
      'gstvaapiencode_h265.c',
      'gstvaapiencode_jpeg.c',
      'gstvaapiencode_mpeg2.c',
      'gstvaapiencode_vp8.c',
    ]
endif

if USE_VP9_ENCODER
  vaapi_sources += 'gstvaapiencode_vp9.c'
endif

gstvaapi = library('gstvaapi',
  vaapi_sources,
  c_args : gstreamer_vaapi_args + ['-DGST_USE_UNSTABLE_API'],
  include_directories : [configinc, libsinc],
  dependencies : [gstbase_dep, gstvideo_dep, gstallocators_dep, gstpbutils_dep,
    libva_dep, gstlibvaapi_dep, gstgl_dep, gstglproto_dep, gstglx11_dep, gstglegl_dep,
    gstglwayland_dep, libm],
  install : true,
  install_dir : plugins_install_dir,
)

plugins = [gstvaapi]
