project('gst-plugins-good', 'c',
  version : '1.23.0.1',
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
gst_version_is_stable = gst_version_minor.is_even()
gst_version_is_dev = gst_version_minor.is_odd() and gst_version_micro < 90

have_cxx = add_languages('cpp', native: false, required: false)
if have_cxx
  cxx = meson.get_compiler('cpp')
endif

glib_req = '>= 2.64.0'
orc_req = '>= 0.4.17'

if gst_version_is_stable
  gst_req = '>= @0@.@1@.0'.format(gst_version_major, gst_version_minor)
else
  gst_req = '>= ' + gst_version
endif

api_version = '1.0'

plugins_install_dir = join_paths(get_option('libdir'), 'gstreamer-1.0')
plugins = []
static_build = get_option('default_library') == 'static'

cc = meson.get_compiler('c')
host_system = host_machine.system()

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
  ]

  if gst_version_is_dev
    # Enable some warnings on MSVC to match GCC/Clang behaviour
    msvc_args += cc.get_supported_arguments([
      '/we4002', # too many actual parameters for macro 'identifier'
      '/we4003', # not enough actual parameters for macro 'identifier'
      '/we4013', # 'function' undefined; assuming extern returning int
      '/we4020', # 'function' : too many actual parameters
      '/we4027', # function declared without formal parameter list
      '/we4029', # declared formal parameter list different from definition
      '/we4033', # 'function' must return a value
      '/we4045', # 'array' : array bounds overflow
      '/we4047', # 'operator' : 'identifier1' differs in levels of indirection from 'identifier2'
      '/we4053', # one void operand for '?:'
      '/we4062', # enumerator 'identifier' in switch of enum 'enumeration' is not handled
      '/we4098', # 'function' : void function returning a value
    ])
  endif
  if have_cxx
    add_project_arguments(msvc_args, language: ['c', 'cpp'])
  else
    add_project_arguments(msvc_args, language: 'c')
  endif
  # Disable SAFESEH with MSVC for plugins and libs that use external deps that
  # are built with MinGW
  noseh_link_args = ['/SAFESEH:NO']
else
  noseh_link_args = []
endif

if cc.has_link_argument('-Wl,-Bsymbolic-functions')
  add_project_link_arguments('-Wl,-Bsymbolic-functions', language : 'c')
endif
if have_cxx and cxx.has_link_argument('-Wl,-Bsymbolic-functions')
  add_project_link_arguments('-Wl,-Bsymbolic-functions', language : 'cpp')
endif

# glib doesn't support unloading, which means that unloading and reloading
# any library that registers static types will fail
if cc.has_link_argument('-Wl,-z,nodelete')
  add_project_link_arguments('-Wl,-z,nodelete', language: 'c')
endif
if have_cxx and cxx.has_link_argument('-Wl,-z,nodelete')
  add_project_link_arguments('-Wl,-z,nodelete', language: 'cpp')
endif

# Symbol visibility
if cc.has_argument('-fvisibility=hidden')
  add_project_arguments('-fvisibility=hidden', language: 'c')
endif

# Disable strict aliasing
if cc.has_argument('-fno-strict-aliasing')
  add_project_arguments('-fno-strict-aliasing', language: 'c')
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

cdata = configuration_data()
cdata.set('ENABLE_NLS', 1)

check_headers = [
  ['HAVE_DLFCN_H', 'dlfcn.h'],
  ['HAVE_FCNTL_H', 'fcntl.h'],
  ['HAVE_INTTYPES_H', 'inttypes.h'],
  ['HAVE_MEMORY_H', 'memory.h'],
  ['HAVE_PROCESS_H', 'process.h'],
  ['HAVE_STDINT_H', 'stdint.h'],
  ['HAVE_STDLIB_H', 'stdlib.h'],
  ['HAVE_STRINGS_H', 'strings.h'],
  ['HAVE_STRING_H', 'string.h'],
  ['HAVE_SYS_IOCTL_H', 'sys/ioctl.h'],
  ['HAVE_SYS_PARAM_H', 'sys/param.h'],
  ['HAVE_SYS_SOCKET_H', 'sys/socket.h'],
  ['HAVE_SYS_STAT_H', 'sys/stat.h'],
  ['HAVE_SYS_TIME_H', 'sys/time.h'],
  ['HAVE_SYS_TYPES_H', 'sys/types.h'],
  ['HAVE_UNISTD_H', 'unistd.h'],
]

foreach h : check_headers
  if cc.has_header(h.get(1))
    cdata.set(h.get(0), 1)
  endif
endforeach

check_functions = [
  ['HAVE_ASINH', 'asinh', '#include<math.h>'],
  ['HAVE_CLOCK_GETTIME', 'clock_gettime', '#include<time.h>'],
  ['HAVE_COSH', 'cosh', '#include<math.h>'],
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
  ['HAVE_DCGETTEXT', 'dcgettext', '#include<libintl.h>'],
# check token HAVE_DIRECTSOUND
# check token HAVE_EXPERIMENTAL
# check token HAVE_EXTERNAL
# check token HAVE_FPCLASS
# check token HAVE_GCC_ASM
  ['HAVE_GETPAGESIZE', 'getpagesize', '#include<unistd.h>'],
# check token HAVE_GETTEXT
# check token HAVE_GST_V4L2
  ['HAVE_ISINF', 'isinf', '#include<math.h>'],
# check token HAVE_LIBV4L2
  ['HAVE_MMAP', 'mmap', '#include<sys/mman.h>'],
  ['HAVE_MMAP64', 'mmap64', '#include<sys/mman.h>'],
# check token HAVE_OSX_AUDIO
# check token HAVE_OSX_VIDEO
# check token HAVE_RDTSC
  ['HAVE_SINH', 'sinh', '#include<math.h>'],
# check token HAVE_WAVEFORM
  ['HAVE_GMTIME_R', 'gmtime_r', '#include<time.h>'],
]

libm = cc.find_library('m', required : false)

foreach f : check_functions
  if cc.has_function(f.get(1), prefix : f.get(2), dependencies : libm)
    cdata.set(f.get(0), 1)
  endif
endforeach

cdata.set('HAVE_IOS', host_system == 'ios')

cdata.set('SIZEOF_CHAR', cc.sizeof('char'))
cdata.set('SIZEOF_INT', cc.sizeof('int'))
cdata.set('SIZEOF_LONG', cc.sizeof('long'))
cdata.set('SIZEOF_SHORT', cc.sizeof('short'))
cdata.set('SIZEOF_VOIDP', cc.sizeof('void*'))
cdata.set('SIZEOF_OFF_T', cc.sizeof('off_t'))

have_rtld_noload = cc.has_header_symbol('dlfcn.h', 'RTLD_NOLOAD')
cdata.set('HAVE_RTLD_NOLOAD', have_rtld_noload)

# Here be fixmes.
# FIXME: check if this is correct
cdata.set('HAVE_CPU_X86_64', host_machine.cpu() == 'amd64')
cdata.set('HAVE_GCC_ASM', cc.get_id() != 'msvc')
cdata.set_quoted('VERSION', gst_version)
cdata.set_quoted('PACKAGE_VERSION', gst_version)
cdata.set_quoted('GST_LICENSE', 'LGPL')
cdata.set_quoted('PACKAGE', 'gst-plugins-good')
cdata.set_quoted('GETTEXT_PACKAGE', 'gst-plugins-good-1.0')
cdata.set_quoted('LOCALEDIR', join_paths(get_option('prefix'), get_option('localedir')))

warning_flags = [
  '-Wmissing-declarations',
  '-Wredundant-decls',
  '-Wwrite-strings',
  '-Winit-self',
  '-Wmissing-include-dirs',
  '-Wno-multichar',
  '-Wvla',
  '-Wpointer-arith',
]

warning_c_flags = [
  '-Wmissing-prototypes',
  '-Wold-style-definition',
  '-Waggregate-return',
]

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

# GStreamer package name and origin url
gst_package_name = get_option('package-name')
if gst_package_name == ''
  if gst_version_nano == 0
    gst_package_name = 'GStreamer Good Plug-ins source release'
  elif gst_version_nano == 1
    gst_package_name = 'GStreamer Good Plug-ins git'
  else
    gst_package_name = 'GStreamer Good Plug-ins prerelease'
  endif
endif
cdata.set_quoted('GST_PACKAGE_NAME', gst_package_name)
cdata.set_quoted('GST_PACKAGE_ORIGIN', get_option('package-origin'))

# Mandatory GST deps
gst_dep = dependency('gstreamer-1.0', version : gst_req,
  fallback : ['gstreamer', 'gst_dep'])
gstbase_dep = dependency('gstreamer-base-1.0', version : gst_req,
  fallback : ['gstreamer', 'gst_base_dep'])
gstnet_dep = dependency('gstreamer-net-1.0', version : gst_req,
  fallback : ['gstreamer', 'gst_net_dep'])
gstcontroller_dep = dependency('gstreamer-controller-1.0', version : gst_req,
  fallback : ['gstreamer', 'gst_controller_dep'])
gstcheck_dep = dependency('gstreamer-check-1.0', version : gst_req,
  required : get_option('tests'),
  fallback : ['gstreamer', 'gst_check_dep'])
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

# GStreamer OpenGL
# FIXME: automagic
gstgl_dep = dependency('gstreamer-gl-1.0', version : gst_req,
    fallback : ['gst-plugins-base', 'gstgl_dep'], required: false)
gstglproto_dep = dependency('', required : false)
gstglx11_dep = dependency('', required : false)
gstglwayland_dep = dependency('', required : false)
gstglegl_dep = dependency('', required : false)
gstglviv_fb_dep = dependency('', required : false)

have_gstgl = gstgl_dep.found()

if have_gstgl
  if gstgl_dep.type_name() == 'pkgconfig'
    gst_gl_apis = gstgl_dep.get_variable('gl_apis').split()
    gst_gl_winsys = gstgl_dep.get_variable('gl_winsys').split()
    gst_gl_platforms = gstgl_dep.get_variable('gl_platforms').split()
  else
    gstbase = subproject('gst-plugins-base')
    gst_gl_apis = gstbase.get_variable('enabled_gl_apis')
    gst_gl_winsys = gstbase.get_variable('enabled_gl_winsys')
    gst_gl_platforms = gstbase.get_variable('enabled_gl_platforms')
  endif

  message('GStreamer OpenGL window systems: @0@'.format(' '.join(gst_gl_winsys)))
  message('GStreamer OpenGL platforms: @0@'.format(' '.join(gst_gl_platforms)))
  message('GStreamer OpenGL apis: @0@'.format(' '.join(gst_gl_apis)))

  foreach ws : ['x11', 'wayland', 'android', 'cocoa', 'eagl', 'win32', 'dispmanx']
    set_variable('gst_gl_have_window_@0@'.format(ws), gst_gl_winsys.contains(ws))
  endforeach
  # Handling viv-fb separately, because the winsys is called "viv-fb", but the
  # variable suffix must be "viv_fb" (dashes are not allowed in variable names).
  set_variable('gst_gl_have_window_viv_fb', gst_gl_winsys.contains('viv-fb'))

  foreach p : ['glx', 'egl', 'cgl', 'eagl', 'wgl']
    set_variable('gst_gl_have_platform_@0@'.format(p), gst_gl_platforms.contains(p))
  endforeach

  foreach api : ['gl', 'gles2']
    set_variable('gst_gl_have_api_@0@'.format(api), gst_gl_apis.contains(api))
  endforeach

  gstglproto_dep = dependency('gstreamer-gl-prototypes-1.0', version : gst_req,
      fallback : ['gst-plugins-base', 'gstglproto_dep'], required: true)
  # Behind specific checks because meson fails at optional dependencies with a
  # fallback to the same subproject.  On the first failure, meson will never
  # check the system again even if the fallback never existed.
  # Last checked with meson 0.54.3
  if gst_gl_have_window_x11
    gstglx11_dep = dependency('gstreamer-gl-x11-1.0', version : gst_req,
       fallback : ['gst-plugins-base', 'gstglx11_dep'], required: true)
  endif
  if gst_gl_have_window_wayland
    gstglwayland_dep = dependency('gstreamer-gl-wayland-1.0', version : gst_req,
        fallback : ['gst-plugins-base', 'gstglwayland_dep'], required: true)
  endif
  if gst_gl_have_platform_egl
    gstglegl_dep = dependency('gstreamer-gl-egl-1.0', version : gst_req,
        fallback : ['gst-plugins-base', 'gstglegl_dep'], required: true)
  endif
  if gst_gl_have_window_viv_fb
    gstglviv_fb_dep = dependency('gstreamer-gl-viv-fb-1.0', version : gst_req,
        fallback : ['gst-plugins-base', 'gstglviv_fb_dep'], required: true)
  endif
endif

zlib_dep = dependency('zlib')
cdata.set('HAVE_ZLIB', true)

gio_dep = dependency('gio-2.0', version: glib_req)
gmodule_dep = dependency('gmodule-no-export-2.0')

gst_plugins_good_args = ['-DHAVE_CONFIG_H']
configinc = include_directories('.')
libsinc = include_directories('gst-libs')

have_orcc = false
orcc_args = []
orc_targets = []
# Used by various libraries/elements that use Orc code
orc_dep = dependency('orc-0.4', version : orc_req, required : get_option('orc'),
    fallback : ['orc', 'orc_dep'])
orcc = find_program('orcc', required : get_option('orc'))
if orc_dep.found() and orcc.found()
  have_orcc = true
  orcc_args = [orcc, '--include', 'glib.h']
  cdata.set('HAVE_ORC', 1)
else
  message('Orc Compiler not found, will use backup C code')
  cdata.set('DISABLE_ORC', 1)
endif

have_nasm = false
# FIXME: nasm path needs testing on non-Linux, esp. Windows
host_cpu = host_machine.cpu_family()
if host_cpu == 'x86_64'
  if cc.get_define('__ILP32__') == '1'
    message('Nasm disabled on x32')
  else
    asm_option = get_option('asm')
    nasm = find_program('nasm', native: true, required: asm_option)
    if nasm.found()
      # We can't use the version: kwarg for find_program because old versions
      # of nasm don't support --version
      ret = run_command(nasm, '-v', check: false)
      if ret.returncode() == 0
        nasm_version = ret.stdout().strip().split()[2]
        nasm_req = '>=2.13'
        if nasm_version.version_compare(nasm_req)
          message('nasm found on x86-64')
          cdata.set('HAVE_NASM', 1)
          have_nasm = true
        else
          if asm_option.enabled()
            error('asm option is enabled, and nasm @0@ was found, but @1@ is required'.format(nasm_version, nasm_req))
          endif
          message('nasm @0@ was found, but @1@ is required'.format(nasm_version, nasm_req))
        endif
      else
        if asm_option.enabled()
          error('asm option is enabled, but nasm is not usable: @0@\n@1@'.format(ret.stdout(), ret.stderr()))
        endif
        message('nasm was found, but it\'s not usable')
      endif
      # Unset nasm to not be 'found'
      if not have_nasm
        nasm = disabler()
      endif
    endif
  endif
endif

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
  if cc.get_argument_syntax() == 'msvc'
    msvc_args = cc.get_supported_arguments([
      '/wd4101', # 'identifier' : unreferenced local variable
      '/wd4189', # 'identifier' : local variable is initialized but not referenced
    ])
    add_project_arguments(msvc_args, language: ['c', 'cpp'])
  else
    if cc.has_argument('-Wno-unused')
      add_project_arguments('-Wno-unused', language: 'c')
    endif
    if have_cxx and cxx.has_argument ('-Wno-unused')
      add_project_arguments('-Wno-unused', language: 'cpp')
    endif
  endif
else
  if cc.get_argument_syntax() == 'msvc' and gst_version_is_dev
    msvc_args = cc.get_supported_arguments([
      '/we4101', # 'identifier' : unreferenced local variable
      '/we4189', # 'identifier' : local variable is initialized but not referenced
    ])
    add_project_arguments(msvc_args, language: ['c', 'cpp'])
  endif
  message('GStreamer debug system is enabled')
endif

presetdir = join_paths(get_option('datadir'), 'gstreamer-' + api_version, 'presets')

python3 = import('python').find_installation()
pkgconfig = import('pkgconfig')
plugins_pkgconfig_install_dir = join_paths(plugins_install_dir, 'pkgconfig')
if get_option('default_library') == 'shared'
  # If we don't build static plugins there is no need to generate pc files
  plugins_pkgconfig_install_dir = disabler()
endif

subdir('gst')
subdir('sys')
subdir('ext')
subdir('tests')
subdir('docs')

if have_orcc and orc_targets.length() > 0
  update_orc_dist_files = find_program('scripts/update-orc-dist-files.py')

  orc_update_targets = []
  foreach t : orc_targets
    orc_name = t.get('name')
    orc_file = t.get('orc-source')
    header = t.get('header')
    source = t.get('source')
    # alias_target() only works with build targets, so can't use run_target() here
    orc_update_targets += [
      custom_target('update-orc-@0@'.format(orc_name),
        input: [header, source],
        command: [update_orc_dist_files, orc_file, header, source],
        output: ['@0@-dist.c'.format(orc_name)]) # not entirely true
    ]
  endforeach

  if orc_update_targets.length() > 0
    update_orc_dist_target = alias_target('update-orc-dist', orc_update_targets)
  endif
endif

# xgettext is optional (on Windows for instance)
if find_program('xgettext', required : get_option('nls')).found()
  subdir('po')
endif

subdir('scripts')

# Set release date
if gst_version_nano == 0
  extract_release_date = find_program('scripts/extract-release-date-from-doap-file.py')
  run_result = run_command(extract_release_date, gst_version, files('gst-plugins-good.doap'), check: true)
  release_date = run_result.stdout().strip()
  cdata.set_quoted('GST_PACKAGE_RELEASE_DATETIME', release_date)
  message('Package release date: ' + release_date)
endif

if gio_dep.version().version_compare('< 2.67.4')
  cdata.set('g_memdup2(ptr,sz)', '(G_LIKELY(((guint64)(sz)) < G_MAXUINT)) ? g_memdup(ptr,sz) : (g_abort(),NULL)')
endif

configure_file(output : 'config.h', configuration : cdata)

meson.add_dist_script('scripts/gen-changelog.py', meson.project_name(), '1.20.0', meson.project_version())

plugin_names = []
gst_plugins = []
foreach plugin: plugins
  # gstsoup is a special case because of the way we build the static/dynamic libraries
  if plugin.name() not in ['gstsoup', 'gstadaptivedemux2']
    pkgconfig.generate(plugin, install_dir: plugins_pkgconfig_install_dir)
  endif
  dep = declare_dependency(link_with: plugin, variables: {'full_path': plugin.full_path()})
  meson.override_dependency(plugin.name(), dep)
  gst_plugins += [dep]
  if plugin.name().startswith('gst')
    plugin_names += [plugin.name().substring(3)]
  else
    plugin_names += [plugin.name()]
  endif
endforeach

summary({
    'Plugins': plugin_names,
}, list_sep: ', ')
