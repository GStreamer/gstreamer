project('gstreamer', 'c',
  version : '1.14.4',
  meson_version : '>= 0.40.1',
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

host_system = host_machine.system()

apiversion = '1.0'
soversion = 0
# maintaining compatibility with the previous libtool versioning
# current = minor * 100 + micro
libversion = '@0@.@1@.0'.format(soversion, gst_version_minor * 100 + gst_version_micro)

prefix = get_option('prefix')
libtype = get_option('library_format')

libexecdir = get_option('libexecdir')
helpers_install_dir = join_paths(libexecdir, 'gstreamer-1.0')

cc = meson.get_compiler('c')

# Ignore several spurious warnings for things gstreamer does very commonly
# If a warning is completely useless and spammy, use '/wdXXXX' to suppress it
# If a warning is harmless but hard to fix, use '/woXXXX' so it's shown once
# NOTE: Only add warnings here if you are sure they're spurious
if cc.get_id() == 'msvc'
  add_project_arguments(
      '/wd4018', # implicit signed/unsigned conversion
      '/wd4146', # unary minus on unsigned (beware INT_MIN)
      '/wd4244', # lossy type conversion (e.g. double -> int)
      '/wd4305', # truncating type conversion (e.g. double -> float)
      language : 'c')
elif cc.has_argument('-Wl,-Bsymbolic-functions')
  # FIXME: Add an option for this if people ask for it
  add_project_link_arguments('-Wl,-Bsymbolic-functions', language : 'c')
endif

# Symbol visibility
have_visibility_hidden = cc.has_argument('-fvisibility=hidden')
if have_visibility_hidden
  add_project_arguments('-fvisibility=hidden', language: 'c')
endif

# Disable strict aliasing
if cc.has_argument('-fno-strict-aliasing')
  add_project_arguments('-fno-strict-aliasing', language: 'c')
endif

cdata = configuration_data()
cdata.set_quoted('GST_API_VERSION', apiversion)
cdata.set_quoted('GST_DATADIR', join_paths(prefix, get_option('datadir')))
cdata.set_quoted('LOCALEDIR', join_paths(prefix, get_option('localedir')))
cdata.set_quoted('LIBDIR', join_paths(prefix, get_option('libdir')))
cdata.set_quoted('GST_API_VERSION', '1.0')
cdata.set_quoted('GETTEXT_PACKAGE', 'gstreamer-1.0')
cdata.set_quoted('GST_LICENSE', 'LGPL')
cdata.set_quoted('PACKAGE', 'gstreamer')
cdata.set_quoted('PACKAGE_NAME', 'GStreamer')
cdata.set_quoted('PACKAGE_STRING', 'GStreamer @0@'.format(gst_version))
cdata.set_quoted('PACKAGE_TARNAME', 'gstreamer')
cdata.set_quoted('PACKAGE_BUGREPORT', 'http://bugzilla.gnome.org/enter_bug.cgi?product=GStreamer')
cdata.set_quoted('PACKAGE_URL', '')
cdata.set_quoted('PACKAGE_VERSION', gst_version)
cdata.set_quoted('PLUGINDIR', join_paths(get_option('prefix'), get_option('libdir'), 'gstreamer-1.0'))
cdata.set_quoted('VERSION', gst_version)
# FIXME: --with-memory-alignment],[8,N,malloc,pagesize (default is 32)]) option
cdata.set('MEMORY_ALIGNMENT_MALLOC', 1)
cdata.set_quoted('GST_PLUGIN_SCANNER_INSTALLED', join_paths(prefix, helpers_install_dir, 'gst-plugin-scanner'))
cdata.set_quoted('GST_PTP_HELPER_INSTALLED', join_paths(prefix, helpers_install_dir, 'gst-ptp-helper'))
cdata.set_quoted('GST_PLUGIN_SCANNER_SUBDIR', libexecdir,
  description: 'libexecdir path component, used to find plugin-scanner on relocatable builds on windows')

if host_system == 'darwin'
  cdata.set_quoted('GST_EXTRA_MODULE_SUFFIX', '.dylib')
endif

if gst_version_nano > 0
    # Have GST_ERROR message printed when running from git
    cdata.set('GST_LEVEL_DEFAULT', 'GST_LEVEL_ERROR')
else
    cdata.set('GST_LEVEL_DEFAULT', 'GST_LEVEL_NONE')
endif

# GStreamer package name and origin url
gst_package_name = get_option('with-package-name')
if gst_package_name == ''
  if gst_version_nano == 0
    gst_package_name = 'GStreamer source release'
  elif gst_version_nano == 1
    gst_package_name = 'GStreamer git'
  else
    gst_package_name = 'GStreamer prerelease'
  endif
endif
cdata.set_quoted('GST_PACKAGE_NAME', gst_package_name)
cdata.set_quoted('GST_PACKAGE_ORIGIN', get_option('with-package-origin'))

# These are only needed/used by the ABI tests
host_defines = [
  [ 'x86', 'HAVE_CPU_I386' ],
  [ 'x86_64', 'HAVE_CPU_X86_64' ],
  [ 'arm', 'HAVE_CPU_ARM' ],
  [ 'aarch64', 'HAVE_CPU_AARCH64' ],
  [ 'mips', 'HAVE_CPU_MIPS' ],
  [ 'powerpc', 'HAVE_CPU_PPC' ],
  [ 'powerpc64', 'HAVE_CPU_PPC64' ],
  [ 'alpha', 'HAVE_CPU_ALPHA' ],
  [ 'sparc', 'HAVE_CPU_SPARC' ],
  [ 'ia64', 'HAVE_CPU_IA64' ],
  [ 'hppa', 'HAVE_CPU_HPPA' ],
  [ 'm68k', 'HAVE_CPU_M68K' ],
  [ 's390', 'HAVE_CPU_S390' ],
]
foreach h : host_defines
  if h.get(0) == host_machine.cpu()
    cdata.set(h.get(1), 1)
  endif
endforeach
# FIXME: should really be called HOST_CPU or such
cdata.set_quoted('TARGET_CPU', host_machine.cpu())

check_headers = [
  'dlfcn.h',
  'inttypes.h',
  'memory.h',
  'poll.h',
  'stdint.h',
  'stdio_ext.h',
  'strings.h',
  'string.h',
  'sys/param.h',
  'sys/poll.h',
  'sys/prctl.h',
  'sys/socket.h',
  'sys/stat.h',
  'sys/times.h',
  'sys/time.h',
  'sys/types.h',
  'sys/utsname.h',
  'sys/wait.h',
  'ucontext.h',
  'unistd.h',
  'valgrind/valgrind.h',
  'sys/resource.h',
]

if host_machine.system() == 'windows'
  check_headers += ['winsock2.h']
endif

foreach h : check_headers
  if cc.has_header(h)
    define = 'HAVE_' + h.underscorify().to_upper()
    cdata.set(define, 1)
  endif
endforeach

if cc.has_member('struct tm', 'tm_gmtoff', prefix : '#include <time.h>')
  cdata.set('HAVE_TM_GMTOFF', 1)
endif

check_functions = [
  'gmtime_r',
  'sigaction',
  'getrusage',
  'fseeko',
  'ftello',
  'poll',
  'ppoll',
  'pselect',
  'getpagesize',
  'clock_gettime',
  # These are needed by libcheck
  'getline',
  'mkstemp',
  'alarm',
  'gettimeofday',
]

foreach f : check_functions
  if cc.has_function(f)
    define = 'HAVE_' + f.underscorify().to_upper()
    cdata.set(define, 1)
  endif
endforeach

if cc.has_function('localtime_r', prefix : '#include<time.h>')
  cdata.set('HAVE_LOCALTIME_R', 1)
  # Needed by libcheck
  cdata.set('HAVE_DECL_LOCALTIME_R', 1)
endif

if cc.links('''#include <pthread.h>
               int main() {
                 pthread_setname_np("example"); return 0;
               }''', name : 'pthread_setname_np(const char*)')
  cdata.set('HAVE_PTHREAD_SETNAME_NP_WITHOUT_TID', 1)
endif

# Check for posix timers and the monotonic clock
time_prefix = '#include <time.h>\n'
if cdata.has('HAVE_UNISTD_H')
  time_prefix += '#include <unistd.h>'
endif

posix_timers_src = time_prefix + '''
#if !defined(_POSIX_TIMERS) || _POSIX_TIMERS < 0 || !defined(CLOCK_REALTIME)
#error Either _POSIX_TIMERS or CLOCK_REALTIME not defined
#endif
'''
if cc.compiles(posix_timers_src, name : 'posix timers from time.h')
  cdata.set('HAVE_POSIX_TIMERS', 1)
endif

monotonic_clock_src = time_prefix + '''
#if !defined(_POSIX_MONOTONIC_CLOCK) || _POSIX_MONOTONIC_CLOCK < 0 || !defined(CLOCK_MONOTONIC)
#error Either _POSIX_MONOTONIC_CLOCK or CLOCK_MONOTONIC not defined
#endif
'''
if cc.compiles(monotonic_clock_src, name : 'monotonic clock from time.h')
  cdata.set('HAVE_MONOTONIC_CLOCK', 1)
endif

# Check for __uint128_t (gcc) by checking for 128-bit division
uint128_t_src = '''int main() {
static __uint128_t v1 = 100;
static __uint128_t v2 = 10;
static __uint128_t u;
u = v1 / v2;
}'''
if cc.compiles(uint128_t_src, name : '__uint128_t available')
  cdata.set('HAVE_UINT128_T', 1)
endif

# All supported platforms have long long now
cdata.set('HAVE_LONG_LONG', 1)

# We only want to use the __declspec(dllexport/import) dance in GST_EXPORT when
# building with MSVC
if cc.get_id() == 'msvc'
  cdata.set('GSTCONFIG_BUILT_WITH_MSVC', 1)
else
  cdata.set('GSTCONFIG_BUILT_WITH_MSVC', 0)
endif

# -------------------------------------------------------------------------------------
# config.h things needed by libcheck
# -------------------------------------------------------------------------------------
if cc.has_function('getpid')
  cdata.set('HAVE_GETPID', 1)
elif host_system == 'windows' and cc.has_function('_getpid')
  cdata.set('HAVE_PROCESS_H', 1) # Used by gstreamer too
  cdata.set('HAVE__GETPID', 1)
endif
if cc.has_function('strdup')
  cdata.set('HAVE_DECL_STRDUP', 1)
elif host_system == 'windows' and cc.has_function('_strdup')
  cdata.set('HAVE__STRDUP', 1) # Windows (MSVC)
endif
if host_machine.system() != 'windows'
  cdata.set('HAVE_FORK', 1)
else
  # libcheck requires HAVE_FORK to be 0 when fork() is not available
  cdata.set('HAVE_FORK', 0)
endif
if cc.has_function('strsignal')
  cdata.set('HAVE_DECL_STRSIGNAL', 1)
endif
# Check for availability of types
if not cc.has_type('clockid_t', prefix : '#include <time.h>')
  cdata.set('clockid_t', 'int')
endif
if not cc.has_type('timer_t', prefix : '#include <time.h>')
  cdata.set('timer_t', 'int')
endif
if not cc.has_members('struct timespec', 'tv_sec', 'tv_nsec',
		      prefix : '#include <time.h>')
  cdata.set('STRUCT_TIMESPEC_DEFINITION_MISSING', 1)
endif
if not cc.has_members('struct itimerspec', 'it_interval', 'it_value',
		      prefix : '#include <time.h>')
  cdata.set('STRUCT_ITIMERSPEC_DEFINITION_MISSING', 1)
endif

# Platform deps; only ws2_32 and execinfo for now
platform_deps = []
if host_machine.system() == 'windows'
  platform_deps = [cc.find_library('ws2_32')]
endif

backtrace_deps = []
if not get_option('disable_libunwind')
  unwind_dep = dependency('libunwind', required : false)
  dw_dep = dependency('libdw', required: false)
  backtrace_deps = [unwind_dep, dw_dep]
  if unwind_dep.found()
    cdata.set('HAVE_UNWIND', 1)
    if dw_dep.found()
      cdata.set('HAVE_DW', 1)
    else
      message('Support for backtraces is partial only.')
    endif
  else
    if cc.has_function('backtrace')
      cdata.set('HAVE_BACKTRACE', 1)
    else
        message('NO backtraces support.')
    endif
  endif
endif

if cc.has_header('execinfo.h')
  if cc.has_function('backtrace', prefix : '#include <execinfo.h>')
    cdata.set('HAVE_BACKTRACE', 1)
  else
    execinfo_dep = cc.find_library('execinfo', required : false)
    if execinfo_dep.found() and cc.has_function('backtrace', prefix : '#include <execinfo.h>', dependencies : execinfo_dep)
      cdata.set('HAVE_BACKTRACE', 1)
      platform_deps += execinfo_dep
    endif
  endif
endif

disable_gst_debug = get_option('disable_gst_debug')
if disable_gst_debug
  add_project_arguments(['-Wno-unused'], language: 'c')
endif

warning_flags = [
  '-Wmissing-declarations',
  '-Wmissing-prototypes',
  '-Wredundant-decls',
  '-Wundef',
  '-Wwrite-strings',
  '-Wformat',
  '-Wformat-nonliteral',
  '-Wformat-security',
  '-Wold-style-definition',
  '-Winit-self',
  '-Wmissing-include-dirs',
  '-Waddress',
  '-Waggregate-return',
  '-Wno-multichar',
  '-Wnested-externs',
  '-Wdeclaration-after-statement',
  '-Wvla',
  '-Wpointer-arith',
]

foreach extra_arg : warning_flags
  if cc.has_argument (extra_arg)
    add_project_arguments([extra_arg], language: 'c')
  endif
endforeach

# Used by the gstutils test
gmp_dep = cc.find_library('gmp', required : false)
cdata.set('HAVE_GMP', gmp_dep.found())
gsl_dep = cc.find_library('gsl', required : false)
gslcblas_dep = cc.find_library('gslcblas', required : false)
cdata.set('HAVE_GSL', gsl_dep.found() and gslcblas_dep.found())
test_deps = [gmp_dep, gsl_dep, gslcblas_dep]

# Used by gstinfo.c
dl_dep = cc.find_library('dl', required : false)
cdata.set('HAVE_DLADDR', cc.has_function('dladdr', dependencies : dl_dep))


configinc = include_directories('.')
libsinc = include_directories('libs')
privinc = include_directories('gst')

# Find dependencies
glib_dep = dependency('glib-2.0', version : '>=2.32.0',
  fallback: ['glib', 'libglib_dep'])
gobject_dep = dependency('gobject-2.0',
  fallback: ['glib', 'libgobject_dep'])
gmodule_dep = dependency('gmodule-2.0',
  fallback: ['glib', 'libgmodule_dep'])
if host_machine.system() == 'windows'
    gio_dep = dependency('gio-2.0',
        fallback: ['glib', 'libgio_dep'])
else
    gio_dep = [dependency('gio-2.0',
                  fallback: ['glib', 'libgio_dep']),
               dependency('gio-unix-2.0',
                  fallback: ['glib', 'libgio_dep'])]
endif

mathlib = cc.find_library('m', required : false)
# Needed for timer_create/settime/delete
# Also provides clock_gettime in glibc < 2.17
rt_lib = cc.find_library('rt', required : false)

gir = find_program('g-ir-scanner', required : false)
gnome = import('gnome')

# Fixme, not very elegant.
build_gir = gir.found() and not meson.is_cross_build() and not get_option('disable_introspection')

gir_init_section = [ '--add-init-section=extern void gst_init(gint*,gchar**);' + \
    'g_setenv("GST_REGISTRY_DISABLE", "yes", TRUE);' + \
    'g_setenv("GST_REGISTRY_1.0", "/no/way/this/exists.reg", TRUE);' + \
    'g_setenv("GST_PLUGIN_PATH_1_0", "", TRUE);' + \
    'g_setenv("GST_PLUGIN_SYSTEM_PATH_1_0", "", TRUE);' + \
    'gst_init(NULL,NULL);' ]

# Used by the *_mkenum.py helper scripts
glib_mkenums = find_program('glib-mkenums')

gst_c_args = ['-DHAVE_CONFIG_H']
if libtype == 'static'
  gst_c_args += ['-DGST_STATIC_COMPILATION']
endif

# Used in gst/parse/meson.build and below
python3 = import('python3').find_python()

bashcomp_dep = dependency('bash-completion', version : '>= 2.0', required : false)

bashcomp_found = bashcomp_dep.found()
bash_completions_dir = ''
bash_helpers_dir = ''

if bashcomp_found
  # get_pkgconfig_variable() won't let us set the prefix
  pkgconfig = find_program('pkg-config')

  runcmd = run_command(pkgconfig,
      '--define-variable=prefix=.',
      '--variable=completionsdir',
      'bash-completion')

  if (runcmd.returncode() == 0)
    bash_completions_dir = runcmd.stdout().strip()
  else
    message('Found bash-completion but the .pc file did not set \'completionsdir\'.')
    bashcomp_found = false
  endif

  runcmd = run_command(pkgconfig,
      '--define-variable=prefix=.',
      '--variable=helpersdir',
      'bash-completion')

  if (runcmd.returncode() == 0)
    bash_helpers_dir = runcmd.stdout().strip()
  else
    message('Found bash-completion, but the .pc file did not set \'helpersdir\'.')
    bashcomp_found = false
  endif
endif

subdir('gst')
subdir('libs')
subdir('plugins')
subdir('tools')
subdir('pkgconfig')
subdir('tests')
subdir('data')

# xgettext is optional (on Windows for instance)
if find_program('xgettext', required : false).found()
  subdir('po')
endif

configure_file(output : 'config.h', configuration : cdata)

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

run_command(python3, '-c', 'import shutil; shutil.copy("hooks/pre-commit.hook", ".git/hooks/pre-commit")')

install_data('gst-element-check-1.0.m4', install_dir : join_paths(get_option('datadir'), 'aclocal'))
