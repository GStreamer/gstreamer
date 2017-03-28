# FIXME: use dependency('valgrind') once we have a method to get the includes
if cc.has_header('valgrind/valgrind.h')
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
  [ 'elements/audioamplify' ],
  [ 'elements/audiochebband' ],
  [ 'elements/audiocheblimit' ],
  [ 'elements/audiodynamic' ],
  [ 'elements/audioecho' ],
  [ 'elements/audiofirfilter' ],
  [ 'elements/audioiirfilter' ],
  [ 'elements/audioinvert' ],
  [ 'elements/audiopanorama' ],
  [ 'elements/audiowsincband' ],
  [ 'elements/audiowsinclimit' ],
  [ 'elements/alphacolor' ],
  [ 'elements/alpha' ],
  [ 'elements/aacparse', false, [libparser_dep] ],
  [ 'elements/ac3parse', false, [libparser_dep] ],
  [ 'elements/amrparse', false, [libparser_dep] ],
  [ 'elements/flacparse', false, [libparser_dep] ],
  [ 'elements/mpegaudioparse', false, [libparser_dep] ],
  [ 'elements/wavpackparse' ],
  [ 'elements/autodetect' ],
  [ 'elements/avimux' ],
  [ 'elements/avisubtitle' ],
  [ 'elements/capssetter' ],
  [ 'elements/deinterlace' ],
  [ 'elements/dtmf' ],
  [ 'pipelines/flacdec', not flac_dep.found() ],
  [ 'elements/flvdemux' ],
  [ 'elements/flvmux' ],
  [ 'elements/qtmux' ],
  [ 'elements/qtdemux' ],
  [ 'elements/mulawdec' ],
  [ 'elements/mulawenc' ],
  [ 'elements/gdkpixbufsink', not gdkpixbuf_dep.found(), [gdkpixbuf_dep] ],
  [ 'elements/gdkpixbufoverlay', not gdkpixbuf_dep.found() ],
  [ 'elements/icydemux' ],
  [ 'elements/id3demux' ],
  [ 'elements/imagefreeze' ],
  [ 'elements/deinterleave' ],
  [ 'elements/interleave' ],
  [ 'elements/jpegdec', not jpeglib.found() ],
  [ 'elements/jpegenc', not jpeglib.found() ],
  [ 'elements/level' ],
  [ 'elements/matroskademux' ],
  [ 'elements/matroskamux' ],
  [ 'elements/matroskaparse' ],
  [ 'elements/multifile' ],
  [ 'elements/splitmux' ],
  [ 'elements/rganalysis' ],
  [ 'elements/rglimiter' ],
  [ 'elements/rgvolume' ],
  [ 'elements/rtp-payloading' ],
  [ 'elements/rtph261' ],
  [ 'elements/rtph263' ],
  [ 'elements/rtpvp9' ],
  [ 'elements/rtpaux' ],
  [ 'elements/rtpbin' ],
  [ 'elements/rtpbin_buffer_list' ],
  [ 'elements/rtpbundle' ],
  [ 'elements/rtpcollision' ],
  [ 'elements/rtpjitterbuffer' ],
  [ 'elements/rtpmux' ],
  [ 'elements/rtprtx' ],
  [ 'elements/rtpsession' ],
  [ 'elements/souphttpsrc', not libsoup_dep.found(), [libsoup_dep] ],
  [ 'elements/spectrum' ],
#  [ 'elements/sunaudio' ],
  [ 'elements/shapewipe' ],
  [ 'elements/id3v2mux', not taglib_dep.found() ],
  [ 'elements/apev2mux', not taglib_dep.found() ],
  [ 'elements/udpsink' ],
  [ 'elements/udpsrc' ],
  [ 'elements/videobox' ],
  [ 'elements/aspectratiocrop' ],
  [ 'elements/videocrop' ],
  [ 'elements/videofilter' ],
  [ 'elements/videomixer' ],
  [ 'elements/vp8enc', not vpx_dep.found() or not have_vp8_encoder ],
  [ 'elements/vp8dec', not vpx_dep.found() or not have_vp8_decoder ],
  [ 'elements/vp9enc', not vpx_dep.found() or not have_vp9_encoder ],
  [ 'pipelines/wavenc' ],
  [ 'elements/wavpackdec', not wavpack_dep.found() ],
  [ 'elements/wavpackenc', not wavpack_dep.found() ],
  [ 'pipelines/wavpack', not wavpack_dep.found() ],
  [ 'elements/wavparse' ],
  [ 'elements/y4menc' ],
  [ 'pipelines/effectv' ],
  [ 'elements/equalizer' ],
  [ 'generic/states' ],
  [ 'pipelines/simple-launch-lines' ],
  [ 'pipelines/tagschecking' ],
]

# FIXME: valgrind elements/rtp-payloading - needs fixing
# elements/videocrop should be disabled since it takes way too long in valgrind

foo='''
if HAVE_ORC
check_orc = orc/deinterlace orc/videomixer orc/videobox
else
check_orc =
endif
'''

test_defines = [
  '-UG_DISABLE_ASSERT',
  '-UG_DISABLE_CAST_CHECKS',
  '-DGST_CHECK_TEST_ENVIRONMENT_BEACON="GST_PLUGIN_LOADING_WHITELIST"',
  '-DGST_TEST_FILES_PATH="' + meson.current_source_dir() + '/../files"',
  '-DGST_USE_UNSTABLE_API',
]

pluginsdirs = []
if gst_dep.type_name() == 'pkgconfig'
  pbase = dependency('gstreamer-plugins-base-' + api_version, required : false)
  pluginsdirs = [gst_dep.get_pkgconfig_variable('pluginsdir'),
                 pbase.get_pkgconfig_variable('pluginsdir')]
endif

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
 autovideosink cacasink cairotextoverlay jackaudiosrc jackaudiosink
 osssrc osssink osxaudiosink osxaudiosrc osxvideosrc osxvideosink
 pulsesink pulsesrc pulsemixer v4l2src'''

# FIXME: check, also + PTHREAD_CFLAGS
test_deps = [gst_dep, gstbase_dep, gstnet_dep, gstcheck_dep, gstaudio_dep,
  gstvideo_dep, gstpbutils_dep, gstrtp_dep, gstrtsp_dep, gsttag_dep,
  gstapp_dep, gio_dep] + glib_deps

# FIXME: add valgrind suppression common/gst.supp gst-plugins-good.supp
foreach t : good_tests
  test_name = t.get(0)
  extra_deps = [ ]
  if t.length() == 3
    extra_deps = t.get(2)
    skip_test = t.get(1)
  elif t.length() == 2
    skip_test = t.get(1)
  else
    skip_test = false
  endif
  if not skip_test
    env = environment()
    env.set('GST_PLUGIN_SYSTEM_PATH_1_0', '')
    env.set('GST_STATE_IGNORE_ELEMENTS', state_ignore_elements)
    env.set('CK_DEFAULT_TIMEOUT', '20')
    env.set('GST_PLUGIN_LOADING_WHITELIST', 'gstreamer', 'gst-plugins-base',
      'gst-plugins-good@' + meson.build_root(), separator: ':')
    env.set('GST_PLUGIN_PATH_1_0', [meson.build_root()] + pluginsdirs)
    env.set('GSETTINGS_BACKEND', 'memory')

    env.set('GST_REGISTRY', '@0@/@1@.registry'.format(meson.current_build_dir(), test_name))
    exe = executable(test_name, '@0@.c'.format(test_name),
      include_directories : [configinc],
      c_args : ['-DHAVE_CONFIG_H=1' ] + test_defines,
      dependencies : [libm] + test_deps + extra_deps,
    )
    test(test_name, exe, env: env, timeout: 3 * 60)
  endif
endforeach

# FIXME: orc tests
fixme_orc_tests='''
orc_deinterlace_CFLAGS = $(ORC_CFLAGS)
orc_deinterlace_LDADD = $(ORC_LIBS) -lorc-test-0.4
nodist_orc_deinterlace_SOURCES = orc/deinterlace.c
orc_videomixer_CFLAGS = $(ORC_CFLAGS)
orc_videomixer_LDADD = $(ORC_LIBS) -lorc-test-0.4
nodist_orc_videomixer_SOURCES = orc/videomixer.c
orc_videobox_CFLAGS = $(ORC_CFLAGS)
orc_videobox_LDADD = $(ORC_LIBS) -lorc-test-0.4
nodist_orc_videobox_SOURCES = orc/videobox.c

orc/deinterlace.c: $(top_srcdir)/gst/deinterlace/tvtime.orc
	$(MKDIR_P) orc/
	$(ORCC) --test -o $@ $<

orc/videomixer.c: $(top_srcdir)/gst/videomixer/videomixerorc.orc
	$(MKDIR_P) orc/
	$(ORCC) --test -o $@ $<

orc/videobox.c: $(top_srcdir)/gst/videobox/gstvideoboxorc.orc
	$(MKDIR_P) orc/
	$(ORCC) --test -o $@ $<
'''
