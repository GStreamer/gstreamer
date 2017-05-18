have_registry = true # FIXME not get_option('disable_registry')

libparser = library('parser',
  'elements/parser.c', 'elements/parser.h',
  install : false,
  dependencies : [gst_dep, gstcheck_dep],
)

libparser_dep = declare_dependency(link_with: libparser,
  sources: ['elements/parser.h'])

exif_dep = dependency('libexif', version : '>= 0.6.16', required : false)

# name, condition when to skip the test and extra dependencies
base_tests = [
  [['elements/aiffparse.c']],
  [['elements/asfmux.c']],
  [['elements/assrender.c'], not ass_dep.found(), [ass_dep]],
  [['elements/audiointerleave.c']],
  [['elements/audiomixer.c']],
  [['elements/autoconvert.c']],
  [['elements/autovideoconvert.c']],
  [['elements/camerabin.c']],
  [['elements/compositor.c']],
  [['elements/curlhttpsink.c'], not curl_dep.found(), [curl_dep]],
  [['elements/curlfilesink.c'], not curl_dep.found(), [curl_dep]],
  [['elements/curlftpsink.c'], not curl_dep.found(), [curl_dep]],
  [['elements/curlsmtpsink.c'], not curl_dep.found(), [curl_dep]],
  [['elements/dash_isoff.c'], not xml2_dep.found(), [xml2_dep]],
  [['elements/dash_mpd.c'], not xml2_dep.found(), [xml2_dep]],
  [['elements/faac.c'], not faac_dep.found() or not cc.has_header_symbol('faac.h', 'faacEncOpen'), [faac_dep]],
  [['elements/faad.c'], not faad_dep.found() or not have_faad_2_7, [faad_dep]],
  [['elements/gdpdepay.c']],
  [['elements/gdppay.c']],
  [['elements/h263parse.c']],
  [['elements/h264parse.c']],
  [['elements/id3mux.c']],
  [['elements/jifmux.c'], not exif_dep.found(), [exif_dep]],
  [['elements/jpegparse.c']],
  [['elements/kate.c'], not kate_dep.found(), [kate_dep]],
  [['elements/mpeg4videoparse.c']],
  [['elements/mpegtsmux.c']],
  [['elements/mpegvideoparse.c']],
  [['elements/mssdemux.c', 'elements/test_http_src.c', 'elements/adaptive_demux_engine.c', 'elements/adaptive_demux_common.c'], not xml28_dep.found(), [xml28_dep]],
  [['elements/mxfdemux.c']],
  [['elements/mxfmux.c']],
  [['elements/pcapparse.c']],
  [['elements/pnm.c']],
  [['elements/schroenc.c'], not schro_dep.found(), [schro_dep]],
  [['elements/rtponvifparse.c']],
  [['elements/rtponviftimestamp.c']],
  [['elements/videoframe-audiolevel.c']],
  [['elements/viewfinderbin.c']],
  [['elements/voaacenc.c'], not voaac_dep.found(), [voaac_dep]],
  [['elements/x265enc.c'], not x265_dep.found(), [x265_dep]],
  [['elements/zbar.c'], not zbar_dep.found(), [zbar_dep]],
]

test_defines = [
  '-UG_DISABLE_ASSERT',
  '-UG_DISABLE_CAST_CHECKS',
  '-DGST_CHECK_TEST_ENVIRONMENT_BEACON="GST_STATE_IGNORE_ELEMENTS"',
  '-DGST_TEST_FILES_PATH="' + meson.current_source_dir() + '/../files"',
  '-DDASH_MPD_DATADIR=' + meson.current_source_dir() + '/elements/dash_mpd_data',
  '-DGST_USE_UNSTABLE_API',
]

test_deps = [gst_dep, gstapp_dep, gstbase_dep, gstbasecamerabin_dep, gstphotography_dep, gstpbutils_dep, gstcontroller_dep, gstaudio_dep, gstvideo_dep, gstrtp_dep, gsturidownloader_dep, gstcheck_dep, gio_dep, glib_dep, libparser_dep]

pluginsdirs = [  ]

if gst_dep.type_name() == 'pkgconfig'
  pluginsdirs = [gst_dep.get_pkgconfig_variable('pluginsdir')] + [gst_base_dep.get_pkgconfig_variable('pluginsdir')]
endif

foreach t : base_tests
  fnames = t.get(0)
  test_name = fnames[0].split('.').get(0)
  skip_test = false
  extra_deps = [ ]

  if t.length() >= 3
    extra_deps = t.get(2)
  endif

  if t.length() >= 2
    skip_test = t.get(1)
  endif

  if not skip_test
    exe = executable(test_name, fnames,
      include_directories : [configinc],
      c_args : ['-DHAVE_CONFIG_H=1' ] + test_defines,
      cpp_args : gst_plugins_bad_args,
      dependencies : [libm] + test_deps + extra_deps,
    )

    env = environment()
    env.set('GST_PLUGIN_PATH_1_0', meson.build_root())
    env.set('GST_PLUGIN_SYSTEM_PATH_1_0', '')
    env.set('CK_DEFAULT_TIMEOUT', '20')
    env.set('GST_STATE_IGNORE_ELEMENTS', '')

    foreach plugindir: pluginsdirs
      env.append('GST_PLUGIN_PATH_1_0', plugindir)
    endforeach
    env.set('GST_REGISTRY', '@0@/@1@.registry'.format(meson.current_build_dir(), test_name))
    test(test_name, exe, env: env, timeout: 3 * 60)
  endif
endforeach
