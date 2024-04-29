project('gst-omx', 'c',
  version : '1.22.12',
  meson_version : '>= 0.62',
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

glib_req = '>= 2.62.0'
gst_req = '>= @0@.@1@.0'.format(gst_version_major, gst_version_minor)
tizil_req = '>= 0.19.0'
api_version = '1.0'

plugins_install_dir = '@0@/gstreamer-1.0'.format(get_option('libdir'))

cc = meson.get_compiler('c')

if cc.get_id() == 'msvc'
  msvc_args = [
      # Ignore several spurious warnings for things gstreamer does very commonly
      # If a warning is completely useless and spammy, use '/wdXXXX' to suppress it
      # If a warning is harmless but hard to fix, use '/woXXXX' so it's shown once
      # NOTE: Only add warnings here if you are sure they're spurious
      '/wd4018', # implicit signed/unsigned conversion
      '/wd4146', # unary minus on unsigned (beware INT_MIN)
      '/wd4244', # lossy type conversion (e.g. double -> int)
      '/wd4305', # truncating type conversion (e.g. double -> float)
      cc.get_supported_arguments(['/utf-8']), # set the input encoding to utf-8

      # Enable some warnings on MSVC to match GCC/Clang behaviour
      '/w14062', # enumerator 'identifier' in switch of enum 'enumeration' is not handled
      '/w14101', # 'identifier' : unreferenced local variable
      '/w14189', # 'identifier' : local variable is initialized but not referenced
  ]
  add_project_arguments(msvc_args, language: 'c')
  # Disable SAFESEH with MSVC for plugins and libs that use external deps that
  # are built with MinGW
  noseh_link_args = ['/SAFESEH:NO']
else
  noseh_link_args = []
endif

# glib doesn't support unloading, which means that unloading and reloading
# any library that registers static types will fail
if cc.has_link_argument('-Wl,-z,nodelete')
  add_project_link_arguments('-Wl,-z,nodelete', language: 'c')
endif

cdata = configuration_data()
check_headers = [
#  ['HAVE_DLFCN_H', 'dlfcn.h'],
#  ['HAVE_FCNTL_H', 'fcntl.h'],
#  ['HAVE_INTTYPES_H', 'inttypes.h'],
#  ['HAVE_MEMORY_H', 'memory.h'],
#  ['HAVE_MSACM_H', 'msacm.h'],
#  ['HAVE_PTHREAD_H', 'pthread.h'],
#  ['HAVE_STDINT_H', 'stdint.h'],
#  ['HAVE_STDLIB_H', 'stdlib.h'],
#  ['HAVE_STRINGS_H', 'strings.h'],
#  ['HAVE_STRING_H', 'string.h'],
#  ['HAVE_SYS_PARAM_H', 'sys/param.h'],
#  ['HAVE_SYS_SOCKET_H', 'sys/socket.h'],
#  ['HAVE_SYS_STAT_H', 'sys/stat.h'],
#  ['HAVE_SYS_TIME_H', 'sys/time.h'],
#  ['HAVE_SYS_TYPES_H', 'sys/types.h'],
#  ['HAVE_SYS_UTSNAME_H', 'sys/utsname.h'],
#  ['HAVE_UNISTD_H', 'unistd.h'],
]

foreach h : check_headers
  if cc.has_header(h.get(1))
    cdata.set(h.get(0), 1)
  endif
endforeach

check_functions = [
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
#  ['HAVE_DCGETTEXT', 'dcgettext'],
# check token HAVE_EXPERIMENTAL
# check token HAVE_EXTERNAL
#  ['HAVE_GETPAGESIZE', 'getpagesize'],
# check token HAVE_GETTEXT
]

foreach f : check_functions
  if cc.has_function(f.get(1))
    cdata.set(f.get(0), 1)
  endif
endforeach

#cdata.set('SIZEOF_CHAR', cc.sizeof('char'))
#cdata.set('SIZEOF_INT', cc.sizeof('int'))
#cdata.set('SIZEOF_LONG', cc.sizeof('long'))
#cdata.set('SIZEOF_SHORT', cc.sizeof('short'))
#cdata.set('SIZEOF_VOIDP', cc.sizeof('void*'))

cdata.set('VERSION', '"@0@"'.format(gst_version))
cdata.set('PACKAGE', '"gst-omx"')
cdata.set('PACKAGE_VERSION', '"@0@"'.format(gst_version))
cdata.set('PACKAGE_BUGREPORT', '"https://gitlab.freedesktop.org/gstreamer/gst-omx/issues/new"')
cdata.set('PACKAGE_NAME', '"GStreamer OMX Plugins"')
cdata.set('GETTEXT_PACKAGE', '"gst-omx-1.0"')
cdata.set('GST_API_VERSION', '"@0@"'.format(api_version))
cdata.set('GST_PACKAGE_NAME', '"GStreamer OpenMAX Plug-ins"')
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
gstcontroller_dep = dependency('gstreamer-controller-1.0', version : gst_req,
  fallback : ['gstreamer', 'gst_controller_dep'])
gstallocators_dep = dependency('gstreamer-allocators-1.0', version : gst_req,
  fallback : ['gst-plugins-base', 'allocators_dep'])

gstpbutils_dep = dependency('gstreamer-pbutils-1.0', version : gst_req,
    fallback : ['gst-plugins-base', 'pbutils_dep'])
gstaudio_dep = dependency('gstreamer-audio-1.0', version : gst_req,
    fallback : ['gst-plugins-base', 'audio_dep'])
gstfft_dep = dependency('gstreamer-fft-1.0', version : gst_req,
    fallback : ['gst-plugins-base', 'fft_dep'])
gsttag_dep = dependency('gstreamer-tag-1.0', version : gst_req,
    fallback : ['gst-plugins-base', 'tag_dep'])
gstvideo_dep = dependency('gstreamer-video-1.0', version : gst_req,
    fallback : ['gst-plugins-base', 'video_dep'])

gstgl_dep = dependency('gstreamer-gl-1.0', version : gst_req,
    fallback : ['gst-plugins-base', 'gstgl_dep'], required : false)

x11_dep = dependency('x11', required : false)

gstcheck_dep = dependency('gstreamer-check-1.0', version : gst_req,
    required : get_option('tests'),
    fallback : ['gstreamer', 'gst_check_dep'])

libm = cc.find_library('m', required : false)
gmodule_dep = dependency('gmodule-no-export-2.0', version: glib_req)

gst_omx_args = ['-DHAVE_CONFIG_H']
configinc = include_directories('.')
omx_header_path = get_option('header_path')
if omx_header_path != ''
  omx_inc = []
  gst_omx_args += ['-I' + omx_header_path]
else
  omx_inc = include_directories (join_paths ('omx', 'openmax'))
endif

default_omx_struct_packing = 0
omx_target = get_option ('target')
if omx_target == 'generic'
  cdata.set('USE_OMX_TARGET_GENERIC', 1)
elif omx_target == 'rpi'
  cdata.set('USE_OMX_TARGET_RPI', 1)
  cdata.set('OMX_SKIP64BIT', 1)
  default_omx_struct_packing = 4

  if gstgl_dep.found()
    if gstgl_dep.type_name() == 'pkgconfig'
      gl_winsys = gstgl_dep.get_variable('gl_winsys').split(' ')
      gl_platforms = gstgl_dep.get_variable('gl_platforms').split(' ')
    elif gstgl_dep.type_name() == 'internal'
      # XXX assume gst-plugins-base was built with dispmanx and egl support
      gl_winsys = ['dispmanx']
      gl_platforms = ['egl']
    else
      error ('unreachable dependency type')
    endif

    if not gl_winsys.contains('dispmanx') or not gl_platforms.contains ('egl')
      gstgl_dep = dependency('', required : false)
    endif
  endif
elif omx_target == 'bellagio'
  cdata.set('USE_OMX_TARGET_BELLAGIO', 1)
elif omx_target == 'zynqultrascaleplus'
  cdata.set('USE_OMX_TARGET_ZYNQ_USCALE_PLUS', 1)
  have_allegro_header = cc.has_header (
      'OMX_Allegro.h',
      args : gst_omx_args,
      include_directories : [omx_inc])
  if not have_allegro_header
    error ('Need Allegro OMX headers to build for Zynq UltraScale+. Use -Dheader_path option to specify the path of those headers.')
  endif
elif omx_target == 'tizonia'
  if omx_header_path != ''
    warning('Ignoring -Dheader_path because path is in tizilheaders.pc')
  endif
  cdata.set('USE_OMX_TARGET_TIZONIA', 1)
  tizil_dep = dependency('tizilheaders', version : tizil_req)
  cdata.set('TIZONIA_LIBDIR', tizil_dep.get_variable('libdir'))
  tizil_includedir = tizil_dep.get_variable('includedir')
  gst_omx_args += ['-I' + tizil_includedir + '/tizonia']
  omx_inc = []
else
  error ('Unsupported omx target specified. Use the -Dtarget option')
endif

message ('OMX target: ' + omx_target)

extra_video_headers = ''
# Check for optional OpenMAX extension headers

if cc.has_header (
    'OMX_VideoExt.h',
    args : gst_omx_args,
    include_directories : [omx_inc])
  extra_video_headers += '''
#include <OMX_VideoExt.h>'''
  cdata.set ('HAVE_VIDEO_EXT', 1)
endif

if cc.has_header (
    'OMX_IndexExt.h',
    args : gst_omx_args,
    include_directories : [omx_inc])
  cdata.set ('HAVE_INDEX_EXT', 1)
endif

if cc.has_header (
    'OMX_ComponentExt.h',
    args : gst_omx_args,
    include_directories : [omx_inc])
  cdata.set ('HAVE_COMPONENT_EXT', 1)
endif

if cc.has_header (
    'OMX_CoreExt.h',
    args : gst_omx_args)
  cdata.set ('HAVE_CORE_EXT', 1)
endif

if cc.has_header (
    'OMX_AudioExt.h',
    args : gst_omx_args)
  cdata.set ('HAVE_AUDIO_EXT', 1)
endif

if cc.has_header (
    'OMX_IVCommonExt.h',
    args : gst_omx_args)
  cdata.set ('HAVE_IV_COMMON_EXT', 1)
endif

if cc.has_header (
    'OMX_ImageExt.h',
    args : gst_omx_args)
  cdata.set ('HAVE_IMAGE_EXT', 1)
endif

if cc.has_header (
    'OMX_OtherExt.h',
    args : gst_omx_args)
  cdata.set ('HAVE_OTHER_EXT', 1)
endif

have_omx_vp8 = cc.has_header_symbol(
    'OMX_Video.h',
    'OMX_VIDEO_CodingVP8',
    prefix : extra_video_headers,
    args : gst_omx_args,
    include_directories : [omx_inc])
if have_omx_vp8
  cdata.set('HAVE_VP8', 1)
endif

have_omx_theora = cc.has_header_symbol(
    'OMX_Video.h',
    'OMX_VIDEO_CodingTheora',
    prefix : extra_video_headers,
    args : gst_omx_args,
    include_directories : [omx_inc])
if have_omx_theora
  cdata.set('HAVE_THEORA', 1)
endif

have_omx_hevc = cc.has_header_symbol(
    'OMX_Video.h',
    'OMX_VIDEO_CodingHEVC',
    prefix : extra_video_headers,
    args : gst_omx_args,
    include_directories : [omx_inc])
if have_omx_hevc
  cdata.set('HAVE_HEVC', 1)
endif

if gstgl_dep.found()
  cdata.set ('HAVE_GST_GL', 1)
endif

if x11_dep.found()
  cdata.set ('HAVE_X11', 1)
endif

omx_struct_packing = get_option ('struct_packing').to_int()
if omx_struct_packing == 0
  omx_struct_packing = default_omx_struct_packing
endif
if omx_struct_packing != 0
  cdata.set('GST_OMX_STRUCT_PACKING', omx_struct_packing)
endif

omx_conf_dir = join_paths (get_option ('prefix'), get_option ('sysconfdir'), 'xdg')
cdata.set_quoted('GST_OMX_CONFIG_DIR', omx_conf_dir)

warning_flags = [
  '-Wmissing-declarations',
  '-Wredundant-decls',
  '-Wwrite-strings',
  '-Winit-self',
  '-Wmissing-include-dirs',
  '-Wno-multichar',
  '-Wvla',
  '-Wpointer-arith',
  '-Wundef',
]

warning_c_flags = [
  '-Wmissing-prototypes',
  '-Wold-style-definition',
  '-Waggregate-return',
]

have_cxx = add_languages('cpp', required : false)

if have_cxx
  cxx = meson.get_compiler('cpp')
endif

foreach extra_arg : warning_flags
  if cc.has_argument (extra_arg)
    add_project_arguments([extra_arg], language: 'c')
  endif
  if have_cxx and cxx.has_argument (extra_arg)
    add_project_arguments([extra_arg], language: 'cpp')
  endif
endforeach

foreach extra_arg : warning_c_flags
  if cc.has_argument (extra_arg)
    add_project_arguments([extra_arg], language: 'c')
  endif
endforeach

# Disable compiler warnings for unused variables and args if gst debug system is disabled
if gst_dep.type_name() == 'internal'
  gst_debug_disabled = not subproject('gstreamer').get_variable('gst_debug')
else
  # We can't check that in the case of subprojects as we won't
  # be able to build against an internal dependency (which is not built yet)
  gst_debug_disabled = cc.has_header_symbol('gst/gstconfig.h', 'GST_DISABLE_GST_DEBUG', dependencies: gst_dep)
endif

if gst_debug_disabled
  message('GStreamer debug system is disabled')
  if cc.has_argument('-Wno-unused')
    add_project_arguments('-Wno-unused', language: 'c')
  endif
  if have_cxx and cxx.has_argument ('-Wno-unused')
    add_project_arguments('-Wno-unused', language: 'cpp')
  endif
else
  message('GStreamer debug system is enabled')
endif

subdir('config')

if not get_option('examples').disabled()
  subdir('examples')
endif

subdir('omx')

if not get_option('tools').disabled()
  subdir('tools')
endif

if not get_option('tests').disabled() and gstcheck_dep.found()
  subdir('tests')
endif
subdir('docs')

# Set release date
if gst_version_nano == 0
  extract_release_date = find_program('scripts/extract-release-date-from-doap-file.py')
  run_result = run_command(extract_release_date, gst_version, files('gst-omx.doap'), check: true)
  release_date = run_result.stdout().strip()
  cdata.set_quoted('GST_PACKAGE_RELEASE_DATETIME', release_date)
  message('Package release date: ' + release_date)
endif

configure_file(output: 'config.h', configuration: cdata)

meson.add_dist_script('scripts/gen-changelog.py', meson.project_name(), '1.20.0', meson.project_version())
