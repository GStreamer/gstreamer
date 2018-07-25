# FIXME: .types file shouldn't have gl types in it if we don't build gstgl,
# but then what about the other files (sections, sgml)
types = configure_file(input : 'gst-plugins-base-libs.types',
  output : 'gst-plugins-base-libs.types',
  copy : true)

ignored_headers = ['pbutils-private.h', 'gsttageditingprivate.h', 'id3v2.h',
  'kiss_fft_f32.h', 'kiss_fft_f64.h', 'kiss_fftr_f32.h', 'kiss_fftr_f64.h',
  'kiss_fftr_s16.h', 'kiss_fftr_s32.h', 'kiss_fft_s16.h', 'kiss_fft_s32.h',
  '_kiss_fft_guts_f32.h', '_kiss_fft_guts_f64.h', '_kiss_fft_guts_s16.h',
  '_kiss_fft_guts_s16.h', '_kiss_fft_guts_s32.h', '_kiss_fft_guts_s32.h',
  'pbutils-marshal.h']

if build_gstgl
  doc_extra_deps = [gstgl_dep]
else
  doc_extra_deps = []
endif

gnome.gtkdoc('gst-plugins-base-libs',
  main_sgml : 'gst-plugins-base-libs-docs.sgml',
  src_dir : '@0@/../../gst-libs/gst'.format(meson.current_source_dir()),
  scan_args : ['--deprecated-guards=GST_DISABLE_DEPRECATED',
               '--ignore-decorators=GST_API|GST_BASE_API|GST_CHECK_API|GST_CONTROLLER_API|GST_NET_API|GST_ALLOCATORS_API|GST_APP_API|GST_AUDIO_API|GST_FFT_API|GST_GL_API|GST_PBUTILS_API|GST_RIFF_API|GST_RTP_API|GST_RTSP_API|GST_SDP_API|GST_TAG_API|GST_VIDEO_API'],
  ignore_headers : ignored_headers,
  scanobjs_args : ['--type-init-func=gst_init(NULL,NULL)'],
  gobject_typesfile : types,
  content_files : [version_entities],
  dependencies : [gst_base_dep, app_dep, allocators_dep, audio_dep,
    fft_dep, pbutils_dep, riff_dep, rtp_dep, rtsp_dep, sdp_dep,
    tag_dep, video_dep] + doc_extra_deps,
  install : true)
