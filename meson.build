project('gst-plugins-base', 'c',
  version : '1.13.90',
  meson_version : '>= 0.36.0',
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

have_cxx = add_languages('cpp', required : false)

glib_req = '>= 2.40.0'
orc_req = '>= 0.4.24'
gst_req = '>= @0@.@1@.0'.format(gst_version_major, gst_version_minor)

api_version = '1.0'
soversion = 0
# maintaining compatibility with the previous libtool versioning
# current = minor * 100 + micro
# FIXME: should be able to convert string to int somehow so we can just do maths
libversion = '@0@.@1@.0'.format(soversion, gst_version_minor * 100 + gst_version_micro)

plugins_install_dir = '@0@/gstreamer-1.0'.format(get_option('libdir'))

cc = meson.get_compiler('c')

if cc.get_id() == 'msvc'
  # Ignore several spurious warnings for things gstreamer does very commonly
  # If a warning is completely useless and spammy, use '/wdXXXX' to suppress it
  # If a warning is harmless but hard to fix, use '/woXXXX' so it's shown once
  # NOTE: Only add warnings here if you are sure they're spurious
  add_project_arguments(
      '/wd4018', # implicit signed/unsigned conversion
      '/wd4146', # unary minus on unsigned (beware INT_MIN)
      '/wd4244', # lossy type conversion (e.g. double -> int)
      '/wd4305', # truncating type conversion (e.g. double -> float)
      language : 'c')
  # Disable SAFESEH with MSVC for plugins and libs that use external deps that
  # are built with MinGW
  noseh_link_args = ['/SAFESEH:NO']
else
  noseh_link_args = []
endif

# Symbol visibility
if cc.has_argument('-fvisibility=hidden')
  add_project_arguments('-fvisibility=hidden', language: 'c')
endif

# Disable strict aliasing
if cc.has_argument('-fno-strict-aliasing')
  add_project_arguments('-fno-strict-aliasing', language: 'c')
endif

core_conf = configuration_data()
check_headers = [
  ['HAVE_DLFCN_H', 'dlfcn.h'],
  ['HAVE_EMMINTRIN_H', 'emmintrin.h'],
  ['HAVE_INTTYPES_H', 'inttypes.h'],
  ['HAVE_MEMORY_H', 'memory.h'],
  ['HAVE_PROCESS_H', 'process.h'],
  ['HAVE_SMMINTRIN_H', 'smmintrin.h'],
  ['HAVE_STDINT_H', 'stdint.h'],
  ['HAVE_STDLIB_H', 'stdlib.h'],
  ['HAVE_STRINGS_H', 'strings.h'],
  ['HAVE_STRING_H', 'string.h'],
  ['HAVE_SYS_SOCKET_H', 'sys/socket.h'],
  ['HAVE_SYS_STAT_H', 'sys/stat.h'],
  ['HAVE_SYS_TYPES_H', 'sys/types.h'],
  ['HAVE_SYS_WAIT_H', 'sys/wait.h'],
  ['HAVE_UNISTD_H', 'unistd.h'],
  ['HAVE_WINSOCK2_H', 'winsock2.h'],
  ['HAVE_XMMINTRIN_H', 'xmmintrin.h'],
]
foreach h : check_headers
  if cc.has_header(h.get(1))
    core_conf.set(h.get(0), 1)
  endif
endforeach

check_functions = [
  ['HAVE_DCGETTEXT', 'dcgettext', '#include<libintl.h>'],
  ['HAVE_GMTIME_R', 'gmtime_r', '#include<time.h>'],
  ['HAVE_LRINTF', 'lrintf', '#include<math.h>'],
  ['HAVE_MMAP', 'mmap', '#include<sys/mman.h>'],
  ['HAVE_LOG2', 'log2', '#include<math.h>'],
]

libm = cc.find_library('m', required : false)
foreach f : check_functions
  if cc.has_function(f.get(1), prefix : f.get(2), dependencies : libm)
    core_conf.set(f.get(0), 1)
  endif
endforeach

core_conf.set('SIZEOF_CHAR', cc.sizeof('char'))
core_conf.set('SIZEOF_INT', cc.sizeof('int'))
core_conf.set('SIZEOF_LONG', cc.sizeof('long'))
core_conf.set('SIZEOF_SHORT', cc.sizeof('short'))
core_conf.set('SIZEOF_VOIDP', cc.sizeof('void*'))

core_conf.set('GETTEXT_PACKAGE', '"gst-plugins-base-1.0"')
core_conf.set('PACKAGE', '"gst-plugins-base"')
core_conf.set('VERSION', '"@0@"'.format(gst_version))
core_conf.set('PACKAGE_VERSION', '"@0@"'.format(gst_version))
core_conf.set('GST_API_VERSION', '"@0@"'.format(api_version))
core_conf.set('GST_INSTALL_PLUGINS_HELPER', '"/FIXME"')
core_conf.set('GST_DATADIR', '"/FIXME"')
core_conf.set('GST_LICENSE', '"LGPL"')

warning_flags = [
  '-Wmissing-declarations',
  '-Wredundant-decls',
  '-Wundef',
  '-Wwrite-strings',
  '-Wformat',
  '-Wformat-nonliteral',
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
]

warning_cxx_flags = [
  '-Waggregate-return',
]

if have_cxx
  cxx = meson.get_compiler('cpp')
  foreach extra_arg : warning_cxx_flags
    if cxx.has_argument (extra_arg)
      add_project_arguments([extra_arg], language: 'cpp')
    endif
  endforeach
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

# GStreamer package name and origin url
gst_package_name = get_option('with-package-name')
if gst_package_name == ''
  if gst_version_nano == 0
    gst_package_name = 'GStreamer Base Plug-ins source release'
  elif gst_version_nano == 1
    gst_package_name = 'GStreamer Base Plug-ins git'
  else
    gst_package_name = 'GStreamer Base Plug-ins prerelease'
  endif
endif
core_conf.set_quoted('GST_PACKAGE_NAME', gst_package_name)
core_conf.set_quoted('GST_PACKAGE_ORIGIN', get_option('with-package-origin'))

# FIXME: These should be configure options
core_conf.set('DEFAULT_VIDEOSINK', '"autovideosink"')
core_conf.set('DEFAULT_AUDIOSINK', '"autoaudiosink"')

# Set whether the audioresampling method should be detected at runtime
core_conf.set('AUDIORESAMPLE_FORMAT_' + get_option('audioresample_format').to_upper(), true)

gst_plugins_base_args = ['-DHAVE_CONFIG_H']
if get_option('default_library') == 'static'
  gst_plugins_base_args += ['-DGST_STATIC_COMPILATION']
endif

# X11 checks are for sys/ and tests/
x11_dep = dependency('x11', required : false)
# GLib checks are for the entire project
giounix_dep = dependency('gio-unix-2.0', version : glib_req, required : false)
# Almost everything that uses glib also uses gobject
glib_deps = [dependency('glib-2.0', version : glib_req, fallback: ['glib', 'libglib_dep']),
             dependency('gobject-2.0', fallback: ['glib', 'libgobject_dep'])]
gio_dep = dependency('gio-2.0', fallback: ['glib', 'libgio_dep'])

core_conf.set('HAVE_X', x11_dep.found())
core_conf.set('HAVE_GIO_UNIX_2_0',
    (gio_dep.type_name() != 'pkgconfig' and host_machine.system() != 'windows')
    or giounix_dep.found())

if gio_dep.type_name() == 'pkgconfig'
    core_conf.set_quoted('GIO_MODULE_DIR',
        gio_dep.get_pkgconfig_variable('giomoduledir'))
    core_conf.set_quoted('GIO_LIBDIR',
        gio_dep.get_pkgconfig_variable('libdir'))
    core_conf.set_quoted('GIO_PREFIX',
        gio_dep.get_pkgconfig_variable('prefix'))
else
    core_conf.set_quoted('GIO_MODULE_DIR', join_paths(get_option('prefix'),
      get_option('libdir'), 'gio/modules'))
    core_conf.set_quoted('GIO_LIBDIR', join_paths(get_option('prefix'),
      get_option('libdir')))
    core_conf.set_quoted('GIO_PREFIX', get_option('prefix'))
endif

configinc = include_directories('.')
libsinc = include_directories('gst-libs')

# To use the subproject make subprojects directory
# and put gstreamer meson git there (symlinking is fine)
gst_dep = dependency('gstreamer-1.0', version : gst_req,
  fallback : ['gstreamer', 'gst_dep'])
gst_base_dep = dependency('gstreamer-base-1.0', version : gst_req,
  fallback : ['gstreamer', 'gst_base_dep'])
gst_net_dep = dependency('gstreamer-net-1.0', version : gst_req,
  fallback : ['gstreamer', 'gst_net_dep'])
if host_machine.system() != 'windows'
  gst_check_dep = dependency('gstreamer-check-1.0', version : gst_req,
    fallback : ['gstreamer', 'gst_check_dep'])
endif
gst_controller_dep = dependency('gstreamer-controller-1.0', version : gst_req,
  fallback : ['gstreamer', 'gst_controller_dep'])

# Used by the *_mkenum.py helper scripts
glib_mkenums = find_program('glib-mkenums')

have_orcc = false
orcc_args = []
if get_option('use_orc') != 'no'
  need_orc = get_option('use_orc') == 'yes'
  # Used by various libraries/elements that use Orc code
  orc_dep = dependency('orc-0.4', version : orc_req, required : need_orc)
  orcc = find_program('orcc', required : need_orc)
  if orc_dep.found() and orcc.found()
    have_orcc = true
    orcc_args = [orcc, '--include', 'glib.h']
    core_conf.set('HAVE_ORC', 1)
  else
    message('Orc Compiler not found, will use backup C code')
    core_conf.set('DISABLE_ORC', 1)
  endif
else
  core_conf.set('DISABLE_ORC', 1)
endif

# Used to build SSE* things in audio-resampler
sse_args = '-msse'
sse2_args = '-msse2'
sse41_args = '-msse4.1'

have_sse = cc.has_argument(sse_args)
have_sse2 = cc.has_argument(sse2_args)
have_sse41 = cc.has_argument(sse41_args)

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
        add_project_arguments('-Wno-unused', language: 'c')
    else
        message('GStreamer debug system is enabled')
    endif
endif

gir = find_program('g-ir-scanner', required : false)
gnome = import('gnome')
build_gir = gir.found() and not meson.is_cross_build() and not get_option('disable_introspection')
gir_init_section = [ '--add-init-section=extern void gst_init(gint*,gchar**);' + \
    'g_setenv("GST_REGISTRY_DISABLE", "yes", TRUE);' + \
    'g_setenv("GST_REGISTRY_1.0", "@0@", TRUE);'.format(meson.current_build_dir() + '/gir_empty_registry.reg') + \
    'g_setenv("GST_PLUGIN_PATH_1_0", "", TRUE);' + \
    'g_setenv("GST_PLUGIN_SYSTEM_PATH_1_0", "", TRUE);' + \
    'gst_init(NULL,NULL);' ]

subdir('gst-libs')
subdir('gst')
subdir('ext')
subdir('sys')
subdir('tools')
subdir('tests')
subdir('pkgconfig')

# xgettext is optional (on Windows for instance)
if find_program('xgettext', required : false).found()
  subdir('po')
endif

if build_machine.system() == 'windows'
  message('Disabling gtk-doc while building on Windows')
elif get_option('disable_gtkdoc')
  message('gtk-doc is disabled via options')
else
  if find_program('gtkdoc-scan', required : false).found()
    subdir('docs')
  else
    message('Not building documentation as gtk-doc was not found')
  endif
endif

# Use core_conf after all subdirs have set values
configure_file(output : 'config.h', configuration : core_conf)

python3 = import('python3').find_python()
run_command(python3, '-c', 'import shutil; shutil.copy("hooks/pre-commit.hook", ".git/hooks/pre-commit")')
