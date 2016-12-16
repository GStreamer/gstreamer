types = configure_file(input : 'gst-plugins-base-libs.types',
  output : 'gst-plugins-base-libs.types',
  configuration : configuration_data())

ignored_headers = ['pbutils-private.h', 'gsttageditingprivate.h', 'id3v2.h',
  'kiss_fft_f32.h', 'kiss_fft_f64.h', 'kiss_fftr_f32.h', 'kiss_fftr_f64.h',
  'kiss_fftr_s16.h', 'kiss_fftr_s32.h', 'kiss_fft_s16.h', 'kiss_fft_s32.h',
  '_kiss_fft_guts_f32.h', '_kiss_fft_guts_f64.h', '_kiss_fft_guts_s16.h',
  '_kiss_fft_guts_s16.h', '_kiss_fft_guts_s32.h', '_kiss_fft_guts_s32.h',
  'pbutils-marshal.h']

gnome.gtkdoc('gst-plugins-base-libs',
  main_sgml : 'gst-plugins-base-libs-docs.sgml',
  src_dir : '@0@/../../gst-libs/gst'.format(meson.current_source_dir()),
  scan_args : ['--deprecated-guards=GST_DISABLE_DEPRECATED',
               '--ignore-decorators=GST_EXPORT'],
  ignore_headers : ignored_headers,
  scanobjs_args : ['--type-init-func=gst_init(NULL,NULL)'],
  gobject_typesfile : types,
  content_files : [version_entities],
  dependencies : [gst_base_dep, app_dep, allocators_dep, audio_dep,
    fft_dep, pbutils_dep, riff_dep, rtp_dep, rtsp_dep, sdp_dep,
    tag_dep, video_dep],
  install : true)
