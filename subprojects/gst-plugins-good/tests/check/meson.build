valgrind_dep = dependency('valgrind', required: false).partial_dependency(compile_args : true)
if valgrind_dep.found()
  cdata.set('HAVE_VALGRIND', 1)
endif

# internal helper lib for unit testing audio parsers
libparser = static_library('libparser', 'elements/parser.c',
  c_args : gst_plugins_good_args + ['-DGST_USE_UNSTABLE_API'],
  include_directories : [configinc],
  dependencies : [gstcheck_dep],
  install : false)

libparser_dep = declare_dependency(link_with : libparser,
  dependencies : gstcheck_dep)

# name, condition when to skip the test and extra dependencies
good_tests = [
  [ 'elements/audioamplify', get_option('audiofx').disabled(), [gstfft_dep] ],
  [ 'elements/audiochebband', get_option('audiofx').disabled(), [gstfft_dep] ],
  [ 'elements/audiocheblimit', get_option('audiofx').disabled(), [gstfft_dep] ],
  [ 'elements/audiodynamic', get_option('audiofx').disabled(), [gstfft_dep] ],
  [ 'elements/audioecho', get_option('audiofx').disabled(), [gstfft_dep] ],
  [ 'elements/audiofirfilter', get_option('audiofx').disabled(), [gstfft_dep] ],
  [ 'elements/audioiirfilter', get_option('audiofx').disabled(), [gstfft_dep] ],
  [ 'elements/audioinvert', get_option('audiofx').disabled(), [gstfft_dep] ],
  [ 'elements/audiopanorama', get_option('audiofx').disabled(), [gstfft_dep] ],
  [ 'elements/audiowsincband', get_option('audiofx').disabled(), [gstfft_dep] ],
  [ 'elements/audiowsinclimit', get_option('audiofx').disabled(), [gstfft_dep] ],
  [ 'elements/alphacolor', get_option('alpha').disabled()],
  [ 'elements/alpha', get_option('alpha').disabled()],
  [ 'elements/avimux', get_option('avi').disabled(), [gstriff_dep] ],
  [ 'elements/avisubtitle', get_option('avi').disabled(), [gstriff_dep] ],
  [ 'elements/capssetter', get_option('debugutils').disabled()],
  [ 'elements/aacparse', get_option('audioparsers').disabled(), [libparser_dep] ],
  [ 'elements/ac3parse', get_option('audioparsers').disabled(), [libparser_dep] ],
  [ 'elements/amrparse', get_option('audioparsers').disabled(), [libparser_dep] ],
  [ 'elements/flacparse', get_option('audioparsers').disabled(), [libparser_dep] ],
  [ 'elements/mpegaudioparse', get_option('audioparsers').disabled(), [libparser_dep] ],
  [ 'elements/autodetect', get_option('autodetect').disabled()],
  [ 'elements/deinterlace', get_option('deinterlace').disabled()],
  [ 'elements/dtmf', get_option('dtmf').disabled()],
  [ 'elements/flvdemux', get_option('flv').disabled()],
  [ 'elements/flvmux', get_option('flv').disabled()],
  [ 'elements/hlsdemux_m3u8' , not hls_dep.found() or not adaptivedemux2_dep.found(), [hls_dep, adaptivedemux2_dep] ],
  [ 'elements/mulawdec', get_option('law').disabled()],
  [ 'elements/mulawenc', get_option('law').disabled()],
  [ 'elements/icydemux', get_option('icydemux').disabled()],
  [ 'elements/id3demux', get_option('id3demux').disabled()],
  [ 'elements/imagefreeze', get_option('imagefreeze').disabled()],
  [ 'elements/deinterleave', get_option('interleave').disabled()],
  [ 'elements/interleave', get_option('interleave').disabled()],
  [ 'elements/level', get_option('level').disabled()],
  [ 'elements/matroskademux', get_option('matroska').disabled(), [gstriff_dep] ],
  [ 'elements/matroskamux', get_option('matroska').disabled(), [gstriff_dep] ],
  [ 'elements/matroskaparse', get_option('matroska').disabled(), [gstriff_dep] ],
  [ 'elements/multifile', get_option('multifile').disabled()],
  [ 'elements/splitmuxsink', get_option('multifile').disabled()],
  [ 'elements/splitmuxsinktimecode', get_option('multifile').disabled()],
  [ 'elements/splitmuxsrc', get_option('multifile').disabled()],
  [ 'elements/qtmux', get_option('isomp4').disabled(), [gstriff_dep, zlib_dep] ],
  [ 'elements/qtdemux', get_option('isomp4').disabled(), [gstriff_dep, zlib_dep] ],
  [ 'elements/rganalysis', get_option('replaygain').disabled()],
  [ 'elements/rglimiter', get_option('replaygain').disabled()],
  [ 'elements/rgvolume', get_option('replaygain').disabled()],
  [ 'elements/spectrum', get_option('spectrum').disabled(), [gstfft_dep] ],
  [ 'elements/shapewipe', get_option('shapewipe').disabled()],
  [ 'elements/udpsink', get_option('udp').disabled()],
  [ 'elements/udpsrc', get_option('udp').disabled()],
  [ 'elements/videobox', get_option('videobox').disabled()],
  [ 'elements/videocrop', get_option('videocrop').disabled()],
  [ 'elements/videofilter', get_option('videofilter').disabled()],
  [ 'elements/videoflip', get_option('videofilter').disabled()],
  [ 'elements/videomixer', get_option('videomixer').disabled()],
  [ 'elements/aspectratiocrop', get_option('videocrop').disabled()],
  [ 'pipelines/wavenc', get_option('wavenc').disabled()],
  [ 'elements/wavparse', get_option('wavparse').disabled(), [gstriff_dep] ],
  [ 'elements/wavpackparse', get_option('audioparsers').disabled()],
  [ 'elements/xingmux', get_option('xingmux').disabled()],
  [ 'elements/y4menc', get_option('y4m').disabled()],
  [ 'pipelines/effectv', get_option('effectv').disabled()],
  [ 'elements/equalizer', get_option('equalizer').disabled()],
  [ 'pipelines/simple-launch-lines' ],
  [ 'pipelines/tagschecking' ],
  [ 'generic/states' ],
]

if not get_option('rtp').disabled() and not get_option('rtpmanager').disabled()
  good_tests += [
    [ 'elements/rtphdrext-colorspace' ],
    [ 'elements/rtph261', ],
    [ 'elements/rtph263' ],
    [ 'elements/rtph264' ],
    [ 'elements/rtph265' ],
    [ 'elements/rtpopus' ],
    [ 'elements/rtppassthrough' ],
    [ 'elements/rtpvp8' ],
    [ 'elements/rtpvp9' ],
    [ 'elements/rtpbin' ],
    [ 'elements/rtpbin_buffer_list' ],
    [ 'elements/rtpcollision' ],
    [ 'elements/rtpfunnel' ],
    [ 'elements/rtphdrextclientaudiolevel', false, [gstsdp_dep, gstaudio_dep] ],
    [ 'elements/rtphdrextsdes', false, [gstrtp_dep, gstsdp_dep] ],
    [ 'elements/rtpjitterbuffer' ],
    [ 'elements/rtpjpeg' ],

    [ 'elements/rtptimerqueue', false, [gstrtp_dep],
      ['../../gst/rtpmanager/rtptimerqueue.c']],

    [ 'elements/rtpmux' ],
    [ 'elements/rtpptdemux' ],
    [ 'elements/rtprtx' ],
    [ 'elements/rtpsession' ],
    [ 'elements/rtpstorage', false, [],  ['../../gst/rtp/gstrtpstorage.c',
					'../../gst/rtp/gstrtpelement.c',
					'../../gst/rtp/gstrtputils.c',
					'../../gst/rtp/rtpstorage.c',
					'../../gst/rtp/rtpstoragestream.c']],
    [ 'elements/rtpred' ],
    [ 'elements/rtpulpfec' ],
    [ 'elements/rtpssrcdemux' ],
    [ 'elements/rtp-payloading' ],
    [ 'elements/rtpst2022-1-fecdec' ],
    [ 'elements/rtpst2022-1-fecenc' ],
  ]
endif

# FIXME: valgrind elements/rtp-payloading - needs fixing
# elements/videocrop should be disabled since it takes way too long in valgrind

libsoup2_dep = dependency('libsoup-2.4', version : '>=2.48',
                          required : false, fallback : ['libsoup', 'libsoup_dep'],
                          default_options: ['sysprof=disabled'])
libsoup3_dep = dependency('libsoup-3.0', required : false,
                           fallback : ['libsoup3', 'libsoup_dep'])

# FIXME: unistd dependency or not tested yet on windows
if host_machine.system() != 'windows'
  good_tests += [
    [ 'elements/amrnbenc', not amrnb_dep.found() ],
    [ 'elements/dash_mpd', not adaptivedemux2_dep.found(), [adaptivedemux2_dep] ],
    [ 'pipelines/flacdec', not flac_dep.found() ],
    [ 'elements/gdkpixbufsink', not gdkpixbuf_dep.found(), [gdkpixbuf_dep] ],
    [ 'elements/gdkpixbufoverlay', not gdkpixbuf_dep.found() ],
    [ 'elements/jpegdec', not jpeglib.found() ],
    [ 'elements/jpegenc', not jpeglib.found() ],
    [ 'elements/mpg123audiodec', not mpg123_dep.found(), [gstfft_dep]],
    [ 'elements/souphttpsrc', not libsoup2_dep.found(), [libsoup2_dep], [], 'elements/souphttpsrc2'],
    [ 'elements/souphttpsrc', not libsoup3_dep.found(), [libsoup3_dep], [], 'elements/souphttpsrc3'],
    [ 'elements/id3v2mux', not taglib_dep.found() ],
    [ 'elements/apev2mux', not taglib_dep.found() ],
    [ 'elements/vp8enc', not vpx_dep.found() or not have_vp8_encoder ],
    [ 'elements/vp8dec', not vpx_dep.found() or not have_vp8_decoder ],
    [ 'elements/vp9enc', not vpx_dep.found() or not have_vp9_encoder ],
    [ 'pipelines/lame', not lame_dep.found() ],
    [ 'elements/wavpackdec', not wavpack_dep.found() ],
    [ 'elements/wavpackenc', not wavpack_dep.found() ],
    [ 'pipelines/wavpack', not wavpack_dep.found() ],
  ]
endif

fsmod = import('fs')
test_defines = [
  '-UG_DISABLE_ASSERT',
  '-UG_DISABLE_CAST_CHECKS',
  '-DGST_CHECK_TEST_ENVIRONMENT_BEACON="GST_PLUGIN_LOADING_WHITELIST"',
  '-DGST_TEST_FILES_PATH="' + fsmod.as_posix(meson.current_source_dir()) + '/../files"',
  '-DGST_USE_UNSTABLE_API',
]

pluginsdirs = []
if gst_dep.type_name() == 'pkgconfig'
  pbase = dependency('gstreamer-plugins-base-' + api_version, required: true)
  pluginsdirs = [gst_dep.get_variable('pluginsdir'),
                 pbase.get_variable('pluginsdir')]
  gst_plugin_scanner_dir = gst_dep.get_variable('pluginscannerdir')
else
  gst_plugin_scanner_dir = subproject('gstreamer').get_variable('gst_scanner_dir')
endif
gst_plugin_scanner_path = join_paths(gst_plugin_scanner_dir, 'gst-plugin-scanner')

# fake device drivers: we could run hardware element tests against dummy drivers
# v4l2: vivo (part of normal kernel)
#   modprobe vivo;
#   gst-launch v4l2src device="/dev/video1" ! xvimagesink;
#   rmmod vivo
#
# alsa: snd-dummy (part of normal alsa, not removable)
#   modprobe snd-dummy;
#   gst-launch alsasrc device="hw:2" ! fakesink
#   gst-launch fakesrc ! alsasink device="hw:2"
#
# need a way to figure out value for the device property
state_ignore_elements = '''aasink autoaudiosrc autoaudiosink autovideosrc
 autovideosink cacasink cairotextoverlay gtkglsink gtksink jackaudiosrc
 jackaudiosink osssrc osssink osxaudiosink osxaudiosrc osxvideosrc osxvideosink
 pulsesink pulsesrc pulsemixer v4l2src'''

# FIXME: check, also + PTHREAD_CFLAGS
test_deps = [gst_dep, gstbase_dep, gstnet_dep, gstcheck_dep, gstaudio_dep,
  gstvideo_dep, gstpbutils_dep, gstrtp_dep, gstrtsp_dep, gsttag_dep,
  gstapp_dep, gio_dep, valgrind_dep]

# FIXME: add valgrind suppression common/gst.supp gst-plugins-good.supp
foreach t : good_tests
  fname = '@0@.c'.format(t.get(0))
  test_name = t.get(4, t.get(0)).underscorify()
  extra_sources = t.get(3, [ ])
  extra_deps = t.get(2, [ ])
  skip_test = t.get(1, false)
  if not skip_test
    env = environment()
    env.set('GST_PLUGIN_SYSTEM_PATH_1_0', '')
    env.set('GST_STATE_IGNORE_ELEMENTS', state_ignore_elements)
    env.set('CK_DEFAULT_TIMEOUT', '20')
    env.set('GST_PLUGIN_LOADING_WHITELIST', 'gstreamer', 'gst-plugins-base',
      'timecode', 'gst-plugins-good@' + meson.project_build_root())
    env.set('GST_PLUGIN_PATH_1_0', [meson.global_build_root()] + pluginsdirs)
    env.set('GSETTINGS_BACKEND', 'memory')

    env.set('GST_REGISTRY', join_paths(meson.current_build_dir(), '@0@.registry'.format(test_name)))
    env.set('GST_PLUGIN_SCANNER_1_0', gst_plugin_scanner_path)
    exe = executable(test_name, fname, extra_sources,
      include_directories : [configinc, libsinc],
      c_args : ['-DHAVE_CONFIG_H=1' ] + test_defines,
      dependencies : [libm] + test_deps + extra_deps,
    )
    test(test_name, exe, env: env, timeout: 3 * 60)
  endif
endforeach

# orc tests
orc_tests = [
  ['orc_audiopanorama', files('../../gst/audiofx/audiopanoramaorc.orc')],
  ['orc_deinterlace', files('../../gst/deinterlace/tvtime.orc')],
  ['orc_videomixer', files('../../gst/videomixer/videomixerorc.orc')],
  ['orc_videobox', files('../../gst/videobox/gstvideoboxorc.orc')],
]

orc_test_dep = dependency('', required : false)
if have_orcc
  # FIXME: there should really just be a separate orc-test-0.4.pc file for this
  if orc_dep.type_name() == 'pkgconfig'
    orc_test_dep = cc.find_library('orc-test-0.4', required : false)
  else
    orc_test_dep = dependency('', fallback: ['orc', 'orc_test_dep'], required: false)
  endif
endif

if have_orcc and orc_test_dep.found()
  foreach t : orc_tests
    tname = t[0]
    torcfile = t[1]
    tcfilename = '@0@.c'.format(tname)
    test_c = custom_target(tcfilename,
      output: tcfilename,
      input: torcfile,
      command: [orcc, '--include', 'stdint.h', '--test', '-o', '@OUTPUT@', '@INPUT@'])
    test_exe = executable(tname, test_c, dependencies: [orc_dep, orc_test_dep])
    test(tname, test_exe)
  endforeach
endif
