project('gst-plugins-bad', 'c', 'cpp',
  version : '1.12.2',
  meson_version : '>= 0.36.0',
  default_options : [ 'warning_level=1',
                      'buildtype=debugoptimized' ])

gst_version = meson.project_version()
version_arr = gst_version.split('.')
gst_version_major = version_arr[0]
gst_version_minor = version_arr[1]
gst_version_micro = version_arr[2]
if version_arr.length() == 4
  gst_version_nano = version_arr[3]
else
  gst_version_nano = 0
endif

glib_req = '>= 2.40.0'
gst_req = '>= @0@.@1@.0'.format(gst_version_major, gst_version_minor)

api_version = '1.0'
soversion = 0
# maintaining compatibility with the previous libtool versioning
# current = minor * 100 + micro
libversion = '@0@.@1@.0'.format(soversion, gst_version_minor.to_int() * 100 + gst_version_micro.to_int())

plugins_install_dir = '@0@/gstreamer-1.0'.format(get_option('libdir'))

cc = meson.get_compiler('c')
cxx = meson.get_compiler('cpp')

if cc.get_id() == 'msvc'
  # Ignore several spurious warnings for things gstreamer does very commonly
  # If a warning is completely useless and spammy, use '/wdXXXX' to suppress it
  # If a warning is harmless but hard to fix, use '/woXXXX' so it's shown once
  # NOTE: Only add warnings here if you are sure they're spurious
  msvc_args = [
      '/wd4018', # implicit signed/unsigned conversion
      '/wd4146', # unary minus on unsigned (beware INT_MIN)
      '/wd4244', # lossy type conversion (e.g. double -> int)
      '/wd4305', # truncating type conversion (e.g. double -> float)
  ]
  add_project_arguments(msvc_args, language : 'c')
  add_project_arguments(msvc_args, language : 'cpp')
  # Disable SAFESEH with MSVC for plugins and libs that use external deps that
  # are built with MinGW
  noseh_link_args = ['/SAFESEH:NO']
else
  if cxx.has_argument('-Wno-non-virtual-dtor')
    add_project_arguments('-Wno-non-virtual-dtor', language: 'cpp')
  endif

  noseh_link_args = []
endif

cdata = configuration_data()
check_headers = [
  ['HAVE_DLFCN_H', 'dlfcn.h'],
  ['HAVE_FCNTL_H', 'fcntl.h'],
  ['HAVE_HIGHGUI_H', 'highgui.h'],
  ['HAVE_INTTYPES_H', 'inttypes.h'],
  ['HAVE_MEMORY_H', 'memory.h'],
  ['HAVE_MSACM_H', 'msacm.h'],
  ['HAVE_NETINET_IN_H', 'netinet/in.h'],
  ['HAVE_NETINET_IP_H', 'netinet/ip.h'],
  ['HAVE_NETINET_TCP_H', 'netinet/tcp.h'],
  ['HAVE_OPENCV2_HIGHGUI_HIGHGUI_C_H', 'opencv2/highgui/highgui_c.h'],
  ['HAVE_PTHREAD_H', 'pthread.h'],
  ['HAVE_STDINT_H', 'stdint.h'],
  ['HAVE_STDLIB_H', 'stdlib.h'],
  ['HAVE_STRINGS_H', 'strings.h'],
  ['HAVE_STRING_H', 'string.h'],
  ['HAVE_SYS_PARAM_H', 'sys/param.h'],
  ['HAVE_SYS_SOCKET_H', 'sys/socket.h'],
  ['HAVE_SYS_STAT_H', 'sys/stat.h'],
  ['HAVE_SYS_TIME_H', 'sys/time.h'],
  ['HAVE_SYS_TYPES_H', 'sys/types.h'],
  ['HAVE_SYS_UTSNAME_H', 'sys/utsname.h'],
  ['HAVE_UNISTD_H', 'unistd.h'],
  ['HAVE_WINDOWS_H', 'windows.h'],
  ['HAVE_WINSOCK2_H', 'winsock2.h'],
  ['HAVE_WS2TCPIP_H', 'ws2tcpip.h'],
]

foreach h : check_headers
  if cc.has_header(h.get(1))
    cdata.set(h.get(0), 1)
  endif
endforeach

check_functions = [
# check token HAVE_ACM
# check token HAVE_ANDROID_MEDIA
# check token HAVE_APEXSINK
# check token HAVE_APPLE_MEDIA
# check token HAVE_ASSRENDER
# check token HAVE_AVC
# check token HAVE_AVFOUNDATION
# check token HAVE_BLUEZ
# check token HAVE_BZ2
# check token HAVE_CFLOCALECOPYCURRENT
# check token HAVE_CFPREFERENCESCOPYAPPVALUE
# check token HAVE_CHROMAPRINT
# check token HAVE_CPU_ALPHA
# check token HAVE_CPU_ARM
# check token HAVE_CPU_CRIS
# check token HAVE_CPU_CRISV32
# check token HAVE_CPU_HPPA
# check token HAVE_CPU_I386
# check token HAVE_CPU_IA64
# check token HAVE_CPU_M68K
# check token HAVE_CPU_MIPS
# check token HAVE_CPU_PPC
# check token HAVE_CPU_PPC64
# check token HAVE_CPU_S390
# check token HAVE_CPU_SPARC
# check token HAVE_CPU_X86_64
# check token HAVE_CURL
# check token HAVE_DAALA
# check token HAVE_DASH
# check token HAVE_DC1394
  ['HAVE_DCGETTEXT', 'dcgettext'],
# check token HAVE_DECKLINK
# check token HAVE_DIRECT3D
# check token HAVE_DIRECTDRAW
# check token HAVE_DIRECTFB
# check token HAVE_DIRECTSOUND
# check token HAVE_DOWHILE_MACROS
# check token HAVE_DTS
# check token HAVE_DVB
# check token HAVE_EXPERIMENTAL
# check token HAVE_EXTERNAL
# check token HAVE_FAAC
# check token HAVE_FAAD
# check token HAVE_FBDEV
# check token HAVE_FLITE
# check token HAVE_FLUIDSYNTH
  ['HAVE_GETPAGESIZE', 'getpagesize'],
# check token HAVE_GETTEXT
# check token HAVE_GL
# check token HAVE_GLCHAR
# check token HAVE_GLEGLIMAGEOES
# check token HAVE_GLINTPTR
# check token HAVE_GLSIZEIPTR
# check token HAVE_GME
  ['HAVE_GMTIME_R', 'gmtime_r'],
# check token HAVE_GRAPHENE
# check token HAVE_GSETTINGS
# check token HAVE_GSM
# check token HAVE_GUDEV
# check token HAVE_HLS
# check token HAVE_ICONV
# check token HAVE_IOS
# check token HAVE_JPEG
# check token HAVE_KATE
# check token HAVE_LADSPA
# check token HAVE_LIBGCRYPT
# check token HAVE_LIBGME_ACCURACY
# check token HAVE_LIBMMS
# check token HAVE_LIBNSL
# check token HAVE_LIBSOCKET
# check token HAVE_LIBUSB
# check token HAVE_LIBVISUAL
# check token HAVE_LINSYS
# check token HAVE_LRDF
# check token HAVE_LV2
# check token HAVE_MIMIC
  ['HAVE_MMAP', 'mmap'],
# check token HAVE_MODPLUG
# check token HAVE_MPEG2ENC
# check token HAVE_MPG123
# check token HAVE_MPLEX
# check token HAVE_MUSEPACK
# check token HAVE_MYTHTV
# check token HAVE_NAS
# check token HAVE_NEON
# check token HAVE_NETTLE
# check token HAVE_OFA
# check token HAVE_OPENAL
# check token HAVE_OPENCV
# check token HAVE_OPENEXR
# check token HAVE_OPENJPEG
# check token HAVE_OPENJPEG_1
# check token HAVE_OPENNI2
# check token HAVE_OPENSLES
# check token HAVE_OPUS
# check token HAVE_ORC
# check token HAVE_OSX
# check token HAVE_OSX_VIDEO
# check token HAVE_PNG
# check token HAVE_PVR
# check token HAVE_QUICKTIME
# check token HAVE_RDTSC
# check token HAVE_RESINDVD
# check token HAVE_RSVG
# check token HAVE_RSVG_2_36_2
# check token HAVE_RTMP
# check token HAVE_SBC
# check token HAVE_SCHRO
# check token HAVE_SDL
# check token HAVE_SHM
# check token HAVE_SMOOTHSTREAMING
# check token HAVE_SNDFILE
# check token HAVE_SNDIO
# check token HAVE_SOUNDTOUCH
# check token HAVE_SOUNDTOUCH_1_4
# check token HAVE_SPANDSP
# check token HAVE_SPC
# check token HAVE_SRTP
# check token HAVE_SSH2
# check token HAVE_TELETEXTDEC
# check token HAVE_TIGER
# check token HAVE_TIMIDITY
# check token HAVE_UVCH264
# check token HAVE_VALGRIND
# check token HAVE_VCD
# check token HAVE_VDPAU
# check token HAVE_VIDEOTOOLBOX
# check token HAVE_VIDEOTOOLBOX_10_9_6
# check token HAVE_VOAACENC
# check token HAVE_VOAMRWBENC
# check token HAVE_WASAPI
# check token HAVE_WAYLAND
# check token HAVE_WEBP
# check token HAVE_WILDMIDI
# check token HAVE_WILDMIDI_0_2_2
# check token HAVE_WIN32
# check token HAVE_WININET
# check token HAVE_WINKS
# check token HAVE_WINSCREENCAP
# check token HAVE_XVID
# check token HAVE_ZBAR
]

foreach f : check_functions
  if cc.has_function(f.get(1))
    cdata.set(f.get(0), 1)
  endif
endforeach

cdata.set('SIZEOF_CHAR', cc.sizeof('char'))
cdata.set('SIZEOF_INT', cc.sizeof('int'))
cdata.set('SIZEOF_LONG', cc.sizeof('long'))
cdata.set('SIZEOF_SHORT', cc.sizeof('short'))
cdata.set('SIZEOF_VOIDP', cc.sizeof('void*'))

cdata.set('VERSION', '"@0@"'.format(gst_version))
cdata.set('PACKAGE', '"gst-plugins-bad"')
cdata.set('PACKAGE_VERSION', '"@0@"'.format(gst_version))
cdata.set('PACKAGE_BUGREPORT', '"http://bugzilla.gnome.org/enter_bug.cgi?product=GStreamer"')
cdata.set('PACKAGE_NAME', '"GStreamer Bad Plug-ins"')
cdata.set('GETTEXT_PACKAGE', '"gst-plugins-bad-1.0"')
cdata.set('GST_API_VERSION', '"@0@"'.format(api_version))
cdata.set('GST_PACKAGE_NAME', '"GStreamer Bad Plug-ins"')
cdata.set('GST_PACKAGE_ORIGIN', '"Unknown package origin"')
cdata.set('GST_LICENSE', '"LGPL"')
cdata.set('LIBDIR', '"@0@"'.format(get_option('libdir')))

# FIXME: This should be exposed as a configuration option
host_system = host_machine.system()
if host_system == 'linux'
  cdata.set('DEFAULT_VIDEOSRC', '"v4l2src"')
elif host_system == 'osx'
  cdata.set('DEFAULT_VIDEOSRC', '"avfvideosrc"')
else
  cdata.set('DEFAULT_VIDEOSRC', '"videotestsrc"')
endif

# Mandatory GST deps
gst_dep = dependency('gstreamer-1.0', version : gst_req,
  fallback : ['gstreamer', 'gst_dep'])
gstbase_dep = dependency('gstreamer-base-1.0', version : gst_req,
  fallback : ['gstreamer', 'gst_base_dep'])
gstnet_dep = dependency('gstreamer-net-1.0', version : gst_req,
  fallback : ['gstreamer', 'gst_net_dep'])
gstcontroller_dep = dependency('gstreamer-controller-1.0', version : gst_req,
  fallback : ['gstreamer', 'gst_controller_dep'])

gstpbutils_dep = dependency('gstreamer-pbutils-1.0', version : gst_req,
    fallback : ['gst-plugins-base', 'pbutils_dep'])
gstallocators_dep = dependency('gstreamer-allocators-1.0', version : gst_req,
    fallback : ['gst-plugins-base', 'allocators_dep'])
gstapp_dep = dependency('gstreamer-app-1.0', version : gst_req,
    fallback : ['gst-plugins-base', 'app_dep'])
gstaudio_dep = dependency('gstreamer-audio-1.0', version : gst_req,
    fallback : ['gst-plugins-base', 'audio_dep'])
gstfft_dep = dependency('gstreamer-fft-1.0', version : gst_req,
    fallback : ['gst-plugins-base', 'fft_dep'])
gstriff_dep = dependency('gstreamer-riff-1.0', version : gst_req,
    fallback : ['gst-plugins-base', 'riff_dep'])
gstrtp_dep = dependency('gstreamer-rtp-1.0', version : gst_req,
    fallback : ['gst-plugins-base', 'rtp_dep'])
gstrtsp_dep = dependency('gstreamer-rtsp-1.0', version : gst_req,
    fallback : ['gst-plugins-base', 'rtsp_dep'])
gstsdp_dep = dependency('gstreamer-sdp-1.0', version : gst_req,
    fallback : ['gst-plugins-base', 'sdp_dep'])
gsttag_dep = dependency('gstreamer-tag-1.0', version : gst_req,
    fallback : ['gst-plugins-base', 'tag_dep'])
gstvideo_dep = dependency('gstreamer-video-1.0', version : gst_req,
    fallback : ['gst-plugins-base', 'video_dep'])

libm = cc.find_library('m', required : false)
glib_dep = dependency('glib-2.0', version : glib_req)
gio_dep = dependency('gio-2.0', version : glib_req)
gmodule_dep = dependency('gmodule-2.0', version : glib_req)
x11_dep = dependency('x11', required : false)

# Used by dtls and hls
openssl_dep = dependency('openssl', version : '>= 1.0.1', required : false)

# Used by mpeg2enc and mplex
# mjpegtools upstream breaks API constantly and doesn't export the version in
# a header anywhere. The configure file has a lot of logic to support old
# versions, but it all seems untested and broken. Require 2.0.0. Can be changed
# if someone complains.
mjpegtools_dep = dependency('mjpegtools', version : '>=2.0.0', required : false)
mjpegtools_api = '0'
if mjpegtools_dep.found()
  mjpegtools_api = '20000'
endif

if x11_dep.found()
  cdata.set('HAVE_X11', 1)
endif

mathlib = cc.find_library('m', required : false)

if host_machine.system() == 'windows'
  winsock2 = [cc.find_library('ws2_32')]
else
  winsock2 = []
endif

have_orcc = false
orcc_args = []
if get_option('use_orc') != 'no'
  need_orc = get_option('use_orc') == 'yes'
  # Used by various libraries/elements that use Orc code
  orc_dep = dependency('orc-0.4', required : need_orc)
  orcc = find_program('orcc', required : need_orc)
  if orc_dep.found() and orcc.found()
    have_orcc = true
    orcc_args = [orcc, '--include', 'glib.h']
    cdata.set('HAVE_ORC', 1)
  else
    message('Orc Compiler not found, will use backup C code')
    cdata.set('DISABLE_ORC', 1)
  endif
else
  cdata.set('DISABLE_ORC', 1)
endif

if gst_dep.type_name() == 'internal'
    gst_proj = subproject('gstreamer')

    if gst_proj.get_variable('disable_gst_debug')
        message('GStreamer debug system is disabled')
        add_project_arguments('-Wno-unused', language: 'c')
    else
        message('GStreamer debug system is enabled')
    endif
else
    # We can't check that in the case of subprojects as we won't
    # be able to build against an internal dependency (which is not built yet)
    if not cc.compiles('''
#include <gst/gstconfig.h>
#ifdef GST_DISABLE_GST_DEBUG
#error "debugging disabled, make compiler fail"
#endif''' , dependencies: gst_dep)
        message('GStreamer debug system is disabled')
        add_global_arguments('-Wno-unused', language: 'c')
    else
        message('GStreamer debug system is enabled')
    endif
endif

configure_file(input : 'config.h.meson',
  output : 'config.h',
  configuration : cdata)

gst_plugins_bad_args = ['-DHAVE_CONFIG_H']
configinc = include_directories('.')
libsinc = include_directories('gst-libs')

vs_module_defs_dir = meson.current_source_dir() + '/win32/common/'

# Used by the *_mkenum.py helper scripts
glib_mkenums = find_program('glib-mkenums')

gir = find_program('g-ir-scanner', required : false)
gnome = import('gnome')
build_gir = gir.found() and not meson.is_cross_build() and not get_option('disable_introspection')
gir_init_section = [ '--add-init-section=extern void gst_init(gint*,gchar**);' + \
    'g_setenv("GST_REGISTRY_1.0", "@0@", TRUE);'.format(meson.current_build_dir() + '/gir_empty_registry.reg') + \
    'g_setenv("GST_PLUGIN_PATH_1_0", "", TRUE);' + \
    'g_setenv("GST_PLUGIN_SYSTEM_PATH_1_0", "", TRUE);' + \
    'gst_init(NULL,NULL);' ]
subdir('gst-libs')
subdir('gst')
subdir('sys')
subdir('ext')
subdir('pkgconfig')

python3 = find_program('python3')
run_command(python3, '-c', 'import shutil; shutil.copy("hooks/pre-commit.hook", ".git/hooks/pre-commit")')
