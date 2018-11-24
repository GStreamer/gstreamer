project('gst-plugins-bad', 'c', 'cpp',
  version : '1.15.0.1',
  meson_version : '>= 0.47',
  default_options : [ 'warning_level=1',
                      'buildtype=debugoptimized' ])

gst_version = meson.project_version()
version_arr = gst_version.split('.')
gst_version_major = version_arr[0].to_int()
gst_version_minor = version_arr[1].to_int()
gst_version_micro = version_arr[2].to_int()
 if version_arr.length() == 4
  gst_version_nano = version_arr[3].to_int()
else
  gst_version_nano = 0
endif
gst_version_is_dev = gst_version_minor % 2 == 1 and gst_version_micro < 90

glib_req = '>= 2.40.0'
gst_req = '>= @0@.@1@.0'.format(gst_version_major, gst_version_minor)

api_version = '1.0'
soversion = 0
# maintaining compatibility with the previous libtool versioning
# current = minor * 100 + micro
curversion = gst_version_minor * 100 + gst_version_micro
libversion = '@0@.@1@.0'.format(soversion, curversion)
osxversion = curversion + 1

plugins_install_dir = '@0@/gstreamer-1.0'.format(get_option('libdir'))

cc = meson.get_compiler('c')
cxx = meson.get_compiler('cpp')

cdata = configuration_data()

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
      cc.get_supported_arguments(['/utf-8']), # set the input encoding to utf-8
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

if cc.has_link_argument('-Wl,-Bsymbolic-functions')
  add_project_link_arguments('-Wl,-Bsymbolic-functions', language : 'c')
endif

# Symbol visibility
if cc.get_id() == 'msvc'
  export_define = '__declspec(dllexport) extern'
elif cc.has_argument('-fvisibility=hidden')
  add_project_arguments('-fvisibility=hidden', language: 'c')
  export_define = 'extern __attribute__ ((visibility ("default")))'
else
  export_define = 'extern'
endif

# Passing this through the command line would be too messy
cdata.set('GST_API_EXPORT', export_define)

# Disable strict aliasing
if cc.has_argument('-fno-strict-aliasing')
  add_project_arguments('-fno-strict-aliasing', language: 'c')
endif
if cxx.has_argument('-fno-strict-aliasing')
  add_project_arguments('-fno-strict-aliasing', language: 'cpp')
endif

# Define G_DISABLE_DEPRECATED for development versions
if gst_version_is_dev
  message('Disabling deprecated GLib API')
  add_project_arguments('-DG_DISABLE_DEPRECATED', language: 'c')
endif

cast_checks = get_option('gobject-cast-checks')
if cast_checks.disabled() or (cast_checks.auto() and not gst_version_is_dev)
  message('Disabling GLib cast checks')
  add_project_arguments('-DG_DISABLE_CAST_CHECKS', language: 'c')
endif

glib_asserts = get_option('glib-asserts')
if glib_asserts.disabled() or (glib_asserts.auto() and not gst_version_is_dev)
  message('Disabling GLib asserts')
  add_project_arguments('-DG_DISABLE_ASSERT', language: 'c')
endif

glib_checks = get_option('glib-checks')
if glib_checks.disabled() or (glib_checks.auto() and not gst_version_is_dev)
  message('Disabling GLib checks')
  add_project_arguments('-DG_DISABLE_CHECKS', language: 'c')
endif

check_headers = [
  ['HAVE_DLFCN_H', 'dlfcn.h'],
  ['HAVE_FCNTL_H', 'fcntl.h'],
  ['HAVE_INTTYPES_H', 'inttypes.h'],
  ['HAVE_MEMORY_H', 'memory.h'],
  ['HAVE_MSACM_H', 'msacm.h'],
  ['HAVE_NETINET_IN_H', 'netinet/in.h'],
  ['HAVE_NETINET_IP_H', 'netinet/ip.h'],
  ['HAVE_NETINET_TCP_H', 'netinet/tcp.h'],
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
# check token HAVE_OSX_VIDEO
  ['HAVE_PIPE2', 'pipe2'],
# check token HAVE_PNG
# check token HAVE_PVR
# check token HAVE_QUICKTIME
# check token HAVE_RDTSC
# check token HAVE_RESINDVD
# check token HAVE_RSVG
# check token HAVE_RSVG_2_36_2
# check token HAVE_RTMP
# check token HAVE_SBC
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
# check token HAVE_VOAACENC
# check token HAVE_VOAMRWBENC
# check token HAVE_WASAPI
# check token HAVE_WAYLAND
# check token HAVE_WEBP
# check token HAVE_WILDMIDI
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

cdata.set_quoted('VERSION', gst_version)
cdata.set_quoted('PACKAGE', 'gst-plugins-bad')
cdata.set_quoted('PACKAGE_VERSION', gst_version)
cdata.set_quoted('PACKAGE_BUGREPORT', 'http://bugzilla.gnome.org/enter_bug.cgi?product=GStreamer')
cdata.set_quoted('PACKAGE_NAME', 'GStreamer Bad Plug-ins')
cdata.set_quoted('GETTEXT_PACKAGE', 'gst-plugins-bad-1.0')
cdata.set_quoted('GST_API_VERSION', api_version)
cdata.set_quoted('GST_LICENSE', 'LGPL')
cdata.set_quoted('LIBDIR', join_paths(get_option('prefix'), get_option('libdir')))
cdata.set_quoted('LOCALEDIR', join_paths(get_option('prefix'), get_option('localedir')))

warning_flags = [
  '-Wmissing-declarations',
  '-Wredundant-decls',
  '-Wwrite-strings',
  '-Wformat',
  '-Wformat-security',
  '-Winit-self',
  '-Wmissing-include-dirs',
  '-Waddress',
  '-Wno-multichar',
  '-Wvla',
  '-Wpointer-arith',
]

warning_c_flags = [
  '-Wmissing-prototypes',
  '-Wdeclaration-after-statement',
  '-Wold-style-definition',
  '-Wnested-externs',
]

warning_cxx_flags = [
  '-Wformat-nonliteral',
]

foreach extra_arg : warning_c_flags
  if cc.has_argument (extra_arg)
    add_project_arguments([extra_arg], language: 'c')
  endif
endforeach

foreach extra_arg : warning_cxx_flags
  if cxx.has_argument (extra_arg)
    add_project_arguments([extra_arg], language: 'cpp')
  endif
endforeach

foreach extra_arg : warning_flags
  if cc.has_argument (extra_arg)
    add_project_arguments([extra_arg], language: 'c')
  endif
  if cxx.has_argument (extra_arg)
    add_project_arguments([extra_arg], language: 'cpp')
  endif
endforeach

# GStreamer package name and origin url
gst_package_name = get_option('package-name')
if gst_package_name == ''
  if gst_version_nano == 0
    gst_package_name = 'GStreamer Bad Plug-ins source release'
  elif gst_version_nano == 1
    gst_package_name = 'GStreamer Bad Plug-ins git'
  else
    gst_package_name = 'GStreamer Bad Plug-ins prerelease'
  endif
endif
cdata.set_quoted('GST_PACKAGE_NAME', gst_package_name)
cdata.set_quoted('GST_PACKAGE_ORIGIN', get_option('package-origin'))

# FIXME: This should be exposed as a configuration option
host_system = host_machine.system()
if host_system == 'linux'
  cdata.set_quoted('DEFAULT_VIDEOSRC', 'v4l2src')
elif ['darwin', 'ios'].contains(host_system)
  cdata.set_quoted('DEFAULT_VIDEOSRC', 'avfvideosrc')
  cdata.set_quoted('GST_EXTRA_MODULE_SUFFIX', '.dylib')
  # Yes, we set this for iOS too. Same as Autotools.
  cdata.set('HAVE_OSX', 1)
else
  cdata.set_quoted('DEFAULT_VIDEOSRC', 'videotestsrc')
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
if host_machine.system() != 'windows'
  gstcheck_dep = dependency('gstreamer-check-1.0', version : gst_req,
    required : get_option('tests'),
    fallback : ['gstreamer', 'gst_check_dep'])
endif

# GStreamer OpenGL
gstgl_dep = dependency('gstreamer-gl-1.0', version : gst_req,
    fallback : ['gst-plugins-base', 'gstgl_dep'], required: get_option('gl'))

if gstgl_dep.found()
  if gstgl_dep.type_name() == 'pkgconfig'
    gst_gl_apis = gstgl_dep.get_pkgconfig_variable('gl_apis').split()
    gst_gl_winsys = gstgl_dep.get_pkgconfig_variable('gl_winsys').split()
    gst_gl_platforms = gstgl_dep.get_pkgconfig_variable('gl_platforms').split()
  else
    gstbase = subproject('gst-plugins-base')
    gst_gl_apis = gstbase.get_variable('enabled_gl_apis')
    gst_gl_winsys = gstbase.get_variable('enabled_gl_winsys')
    gst_gl_platforms = gstbase.get_variable('enabled_gl_platforms')
  endif

  message('GStreamer OpenGL window systems: @0@'.format(' '.join(gst_gl_winsys)))
  message('GStreamer OpenGL platforms: @0@'.format(' '.join(gst_gl_platforms)))
  message('GStreamer OpenGL apis: @0@'.format(' '.join(gst_gl_apis)))

  foreach ws : ['x11', 'wayland', 'android', 'cocoa', 'eagl', 'win32', 'dispmanx', 'viv_fb']
    set_variable('gst_gl_have_window_@0@'.format(ws), gst_gl_winsys.contains(ws))
  endforeach

  foreach p : ['glx', 'egl', 'cgl', 'eagl', 'wgl']
    set_variable('gst_gl_have_platform_@0@'.format(p), gst_gl_platforms.contains(p))
  endforeach

  foreach api : ['opengl', 'gles2']
    set_variable('gst_gl_have_api_@0@'.format(api), gst_gl_apis.contains(api))
  endforeach
endif

libm = cc.find_library('m', required : false)
glib_dep = dependency('glib-2.0', version : glib_req, fallback: ['glib', 'libglib_dep'])
gmodule_dep = dependency('gmodule-2.0', fallback: ['glib', 'libgmodule_dep'])
gio_dep = dependency('gio-2.0', fallback: ['glib', 'libgio_dep'])
# gio-unix-2.0 is used by sys/bluez

# Optional dep of ext/gl and gst/librfb
x11_dep = dependency('x11', required : get_option('x11'))
if x11_dep.found()
  cdata.set('HAVE_X11', 1)
endif

if host_machine.system() == 'windows'
  winsock2 = [cc.find_library('ws2_32')]
else
  winsock2 = []
endif

if ['darwin', 'ios'].contains(host_system)
  add_languages('objc')
  if host_system == 'ios'
    cdata.set('HAVE_IOS', 1)
  endif

  avfoundation_dep = dependency('AVFoundation', required : false)
  if avfoundation_dep.found()
      cdata.set('HAVE_AVFOUNDATION', 1)
  endif

  videotoolbox_dep = dependency('VideoToolbox', required : false)
  if videotoolbox_dep.found()
      cdata.set('HAVE_VIDEOTOOLBOX', 1)
  endif

#  FIXME: framework.version() returns 'unknown'
#  if videotoolbox_dep.version().version_compare('>=10.9.6')
#    cdata.set('HAVE_VIDEOTOOLBOX_10_9_6', 1)
#  endif
endif

have_orcc = false
orcc_args = []
# Used by various libraries/elements that use Orc code
orc_dep = dependency('orc-0.4', required : get_option('orc'))
orcc = find_program('orcc', required : get_option('orc'))
if orc_dep.found() and orcc.found()
  have_orcc = true
  orcc_args = [orcc, '--include', 'glib.h']
  cdata.set('HAVE_ORC', 1)
else
  message('Orc Compiler not found or disabled, will use backup C code')
  cdata.set('DISABLE_ORC', 1)
endif

gnustl_dep = declare_dependency()
if host_system == 'android'
  gnustl_dep = dependency('gnustl', required : false)
endif

if gst_dep.type_name() == 'internal'
  gst_debug_disabled = not subproject('gstreamer').get_variable('gst_debug')
else
  # We can't check that in the case of subprojects as we won't
  # be able to build against an internal dependency (which is not built yet)
  gst_debug_disabled = cc.has_header_symbol('gst/gstconfig.h', 'GST_DISABLE_GST_DEBUG', dependencies: gst_dep)
endif

if gst_debug_disabled and cc.has_argument('-Wno-unused')
  add_project_arguments('-Wno-unused', language: 'c')
endif

gst_plugins_bad_args = ['-DHAVE_CONFIG_H']
configinc = include_directories('.')
libsinc = include_directories('gst-libs')

python3 = import('python').find_installation()

gir = find_program('g-ir-scanner', required : get_option('introspection'))
gnome = import('gnome')
build_gir = gir.found() and not meson.is_cross_build()
gir_init_section = [ '--add-init-section=extern void gst_init(gint*,gchar**);' + \
    'g_setenv("GST_REGISTRY_1.0", "@0@", TRUE);'.format(meson.current_build_dir() + '/gir_empty_registry.reg') + \
    'g_setenv("GST_PLUGIN_PATH_1_0", "", TRUE);' + \
    'g_setenv("GST_PLUGIN_SYSTEM_PATH_1_0", "", TRUE);' + \
    'gst_init(NULL,NULL);' ]

pkgconfig = import('pkgconfig')
plugins_pkgconfig_install_dir = join_paths(plugins_install_dir, 'pkgconfig')
if get_option('default_library') == 'shared'
  # If we don't build static plugins there is no need to generate pc files
  plugins_pkgconfig_install_dir = disabler()
endif

subdir('gst-libs')
subdir('gst')
subdir('sys')
subdir('ext')
subdir('tests')
subdir('pkgconfig')

# xgettext is optional (on Windows for instance)
if find_program('xgettext', required : get_option('nls')).found()
  cdata.set('ENABLE_NLS', 1)
  subdir('po')
endif

configure_file(output : 'config.h', configuration : cdata)

run_command(python3, '-c', 'import shutil; shutil.copy("hooks/pre-commit.hook", ".git/hooks/pre-commit")')
