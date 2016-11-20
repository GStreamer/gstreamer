project('gstreamer', 'c', 'cpp',
  version : '1.10.1',
  meson_version : '>= 0.35.0',
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

apiversion = '1.0'
soversion = 0
# maintaining compatibility with the previous libtool versioning
# current = minor * 100 + micro
libversion = '@0@.@1@.0'.format(soversion, gst_version_minor.to_int() * 100 + gst_version_micro.to_int())

prefix = get_option('prefix')
libtype = get_option('library_format')

libexecdir = get_option('libexecdir')
helpers_install_dir = libexecdir + '/gstreamer-1.0/'

cc = meson.get_compiler('c')

# FIXME: Meson should have a way for portably adding -fPIC when needed for use
# with static libraries that are linked into shared libraries. Or, it should
# add it by default with an option to turn it off if needed.
pic_args = ['-fPIC']
if host_machine.system() == 'windows'
  pic_args = []
endif

# Ignore several spurious warnings for things gstreamer does very commonly
# If a warning is completely useless and spammy, use '/wdXXXX' to suppress it
# If a warning is harmless but hard to fix, use '/woXXXX' so it's shown once
# NOTE: Only add warnings here if you are sure they're spurious
if cc.get_id() == 'msvc'
  add_global_arguments('/wd4018', '/wd4244', '/wd4996', language : 'c')
endif

cdata = configuration_data()
cdata.set('GST_VERSION_MAJOR', gst_version_major)
cdata.set('GST_VERSION_MINOR', gst_version_minor)
cdata.set('GST_VERSION_MICRO', gst_version_micro)
cdata.set('GST_VERSION_NANO', gst_version_nano)
cdata.set('GST_API_VERSION', '"@0@"'.format(apiversion))
cdata.set('GST_DATADIR', '"@0@/@1@"'.format(prefix, get_option('datadir')))
cdata.set('LOCALEDIR', '"@0@/@1@"'.format(prefix, get_option('localedir')))
cdata.set('LIBDIR', '"@0@/@1@"'.format(prefix, get_option('libdir')))
cdata.set('GST_API_VERSION', '"1.0"')
cdata.set('GETTEXT_PACKAGE', '"gstreamer-1.0"')
cdata.set('GST_LEVEL_DEFAULT', 'GST_LEVEL_NONE')
cdata.set('GST_LICENSE', '"LGPL"')
cdata.set('GST_PACKAGE_ORIGIN', '"Unknown package origin"')
cdata.set('GST_PACKAGE_NAME', '"GStreamer source release"')
cdata.set('PACKAGE', '"gstreamer"')
cdata.set('PACKAGE_NAME', '"GStreamer"')
cdata.set('PACKAGE_STRING', '"GStreamer @0@"'.format(gst_version))
cdata.set('PACKAGE_TARNAME', '"gstreamer"')
cdata.set('PACKAGE_BUGREPORT', '"http://bugzilla.gnome.org/enter_bug.cgi?product=GStreamer"')
cdata.set('PACKAGE_URL', '""')
cdata.set('PACKAGE_VERSION', '"@0@"'.format(gst_version))
cdata.set('PLUGINDIR', '"@0@/@1@/gstreamer-1.0"'.format(get_option('prefix'),get_option('libdir')))
cdata.set('VERSION', '"@0@"'.format(gst_version))
# FIXME: --with-memory-alignment],[8,N,malloc,pagesize (default is 32)]) option
cdata.set('MEMORY_ALIGNMENT_MALLOC', 1)
cdata.set('GST_PLUGIN_SCANNER_INSTALLED', '"@0@/@1@/gst-plugin-scanner"'.format(prefix, helpers_install_dir))
cdata.set('GST_PTP_HELPER_INSTALLED', '"@0@/@1@/gst-ptp-helper"'.format(prefix, helpers_install_dir))

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
cdata.set('TARGET_CPU', '"@0@"'.format(host_machine.cpu()))

check_headers = [['dlfcn.h','HAVE_DLFCN_H'],
  ['inttypes.h', 'HAVE_INTTYPES_H'],
  ['memory.h', 'HAVE_MEMORY_H'],
  ['poll.h', 'HAVE_POLL_H'],
  ['stdint.h', 'HAVE_STDINT_H'],
  ['stdio_ext.h', 'HAVE_STDIO_EXT_H'],
  ['strings.h', 'HAVE_STRINGS_H'],
  ['string.h', 'HAVE_STRING_H'],
  ['sys/param.h', 'HAVE_SYS_PARAM_H'],
  ['sys/poll.h', 'HAVE_SYS_POLL_H'],
  ['sys/prctl.h', 'HAVE_SYS_PRCTL_H'],
  ['sys/socket.h', 'HAVE_SYS_SOCKET_H'],
  ['sys/stat.h', 'HAVE_SYS_STAT_H'],
  ['sys/times.h', 'HAVE_SYS_TIMES_H'],
  ['sys/time.h', 'HAVE_SYS_TIME_H'],
  ['sys/types.h', 'HAVE_SYS_TYPES_H'],
  ['sys/utsname.h', 'HAVE_SYS_UTSNAME_H'],
  ['sys/wait.h', 'HAVE_SYS_WAIT_H'],
  ['ucontext.h', 'HAVE_UCONTEXT_H'],
  ['unistd.h', 'HAVE_UNISTD_H'],
  ['valgrind/valgrind.h', 'HAVE_VALGRIND_VALGRIND_H'],
  ['sys/resource.h', 'HAVE_SYS_RESOURCE_H'],
]

if host_machine.system() == 'windows'
  check_headers += [ ['winsock2.h', 'HAVE_WINSOCK2_H'] ]
endif

foreach h : check_headers
  if cc.has_header(h.get(0))
    cdata.set(h.get(1), 1)
  endif
endforeach

gmtoff_src = '''#include <time.h>
int main(void) {
  struct tm t;
  t.tm_gmtoff = 0;
  return 0;
}
'''

if cc.compiles(gmtoff_src, name : 'tm_gmtoff from time.h')
  cdata.set('HAVE_TM_GMTOFF', 1)
endif

if cc.has_function('gmtime_r', prefix : '#include<time.h>')
  cdata.set('HAVE_GMTIME_R', 1)
endif
if cc.has_function('localtime_r', prefix : '#include<time.h>')
  cdata.set('HAVE_LOCALTIME_R', 1)
endif
if cc.has_function('sigaction', prefix : '#include<signal.h>')
  cdata.set('HAVE_SIGACTION', 1)
endif
have_getrusage = cc.has_function('getrusage', prefix : '#include <sys/time.h>\n#include <sys/resource.h>')
if have_getrusage
  cdata.set('HAVE_GETRUSAGE', 1)
endif

if cc.has_function('fseeko', prefix : '#include<stdio.h>')
  cdata.set('HAVE_FSEEKO', 1)
endif
if cc.has_function('ftello', prefix : '#include<stdio.h>')
  cdata.set('HAVE_FTELLO', 1)
endif
if cc.has_function('fsetpos', prefix : '#include<stdio.h>')
  cdata.set('HAVE_FSETPOS', 1)
endif
if cc.has_function('fgetpos', prefix : '#include<stdio.h>')
  cdata.set('HAVE_FGETPOS', 1)
endif
if cc.has_function('poll', prefix : '#include<poll.h>')
  cdata.set('HAVE_POLL', 1)
endif
if cc.has_function('pselect', prefix : '#include<sys/select.h>')
  cdata.set('HAVE_PSELECT', 1)
endif
cdata.set('HAVE_MMAP', 1)

if cc.has_function('posix_memalign', prefix : '#include<stdlib.h>')
  cdata.set('HAVE_POSIX_MEMALIGN', 1)
endif
if cc.has_function('getpagesize', prefix : '#include<unistd.h>')
  cdata.set('HAVE_GETPAGESIZE', 1)
endif

# With XCode 8, clock_gettime will be incorrectly detected as being available
# regardless of what version of OS X you target because the symbol is available
# in the .tbd file as a weak symbol.
# See: https://bugzilla.gnome.org/show_bug.cgi?id=772451 
#
# We cannot simply do cc.has_function with -Wl,-no_weak_imports because the
# check does its own prototype decl that doesn't trigger that compiler flag.
#
# It's only starting from macOS 10.12 and iOS 10.0 that clock_gettime is
# actually available, so we can unconditionally disable it for older versions.
disable_clock_gettime_src = '''#include <AvailabilityMacros.h>
#include <TargetConditionals.h>
#if defined(TARGET_OS_MAC)
#  if MAC_OS_X_VERSION_MIN_REQUIRED < MAC_OS_X_VERSION_10_12
#    error "Not compiling for OS X 10.12 or later"
#  endif
#else
#  if __IPHONE_OS_VERSION_MIN_REQUIRED < __IPHONE_10_0
#    error "Not compiling for iOS 10.0 or later"
#  endif
#endif
'''

if cc.has_function('clock_gettime', prefix : '#include <time.h>')
  if host_machine.system() == 'darwin'
    if cc.compiles(disable_clock_gettime_src, name : 'target darwin/ios version has clock_gettime')
      cdata.set('HAVE_CLOCK_GETTIME', 1)
    endif
  else
    cdata.set('HAVE_CLOCK_GETTIME', 1)
  endif
endif

# We only want to use the __declspec(dllexport/import) dance in GST_EXPORT when
# building with MSVC
if cc.get_id() == 'msvc'
  cdata.set('GSTCONFIG_BUILT_WITH_MSVC', 1)
else
  cdata.set('GSTCONFIG_BUILT_WITH_MSVC', 0)
endif

# -------------------------------------------------------------------------------------
# config.h things needed by libcheck (FIXME: move into the libcheck meson.build) (tpm)
# -------------------------------------------------------------------------------------
# FIXME: check if it is _getpid or getpid on windows (tpm)
if cc.has_function('getpid', prefix : '#include <sys/types.h>\n#include <unistd.h>')
  cdata.set('HAVE_GETPID', 1)
elif cc.has_function('_getpid', prefix : '#include <process.h>')
  cdata.set('HAVE__GETPID', 1)
endif
# FIXME: check for _strdup() but how/where and with what includes? (windows?) (tpm)
if cc.has_function('strdup', prefix : '#include <string.h>')
  cdata.set('HAVE_DECL_STRDUP', 1)
elif cc.has_function('_strdup', prefix : '#include <string.h>')
  cdata.set('HAVE__STRDUP', 1)
endif
if cc.has_function('mkstemp', prefix : '#include <stdlib.h>')
  cdata.set('HAVE_MKSTEMP', 1)
endif
if host_machine.system() != 'windows'
  cdata.set('HAVE_FORK', 1)
else
  # libcheck requires HAVE_FORK to be 0 when fork() is not available
  cdata.set('HAVE_FORK', 0)
endif
if cc.has_function('alarm', prefix : '#include <unistd.h>')
  cdata.set('HAVE_ALARM', 1)
endif
if cc.has_function('localtime_r', prefix : '#include <time.h>')
  cdata.set('HAVE_DECL_LOCALTIME_R', 1)
endif
if cc.has_function('strsignal', prefix : '#include <string.h>')
  cdata.set('HAVE_DECL_STRSIGNAL', 1)
endif

configure_file(input : 'config.h.meson',
  output : 'config.h',
  configuration : cdata)


configinc = include_directories('.')
libsinc = include_directories('libs')
privinc = include_directories('gst')

# Find dependencies
glib_dep = dependency('glib-2.0', version : '>=2.32.0')
gobject_dep = dependency('gobject-2.0')
gmodule_dep = dependency('gmodule-2.0')
if host_machine.system() == 'windows'
    gio_dep = dependency('gio-2.0')
else
    gio_dep = [dependency('gio-2.0'), dependency('gio-unix-2.0')]
endif

mathlib = cc.find_library('m', required : false)
rt_lib = cc.find_library('rt', required : false) # clock_gettime

# Platform deps; only ws2_32 for now
platform_deps = []
if host_machine.system() == 'windows'
  platform_deps = [cc.find_library('ws2_32')]
endif

gir = find_program('g-ir-scanner', required : false)
gnome = import('gnome')

# Fixme, not very elegant.
build_gir = gir.found() and not meson.is_cross_build() and not get_option('disable_introspection')

gir_init_section = [ '--add-init-section=extern void gst_init(gint*,gchar**);' + \
    'g_setenv("GST_REGISTRY_1.0", "/no/way/this/exists.reg", TRUE);' + \
    'g_setenv("GST_PLUGIN_PATH_1_0", "", TRUE);' + \
    'g_setenv("GST_PLUGIN_SYSTEM_PATH_1_0", "", TRUE);' + \
    'gst_init(NULL,NULL);' ]
vs_module_defs_dir = meson.current_source_dir() + '/win32/common/'

gst_c_args = ['-DHAVE_CONFIG_H']
if libtype == 'static'
  gst_c_args += ['-DGST_STATIC_COMPILATION']
endif

subdir('gst')
subdir('libs')
subdir('plugins')
subdir('tools')
subdir('pkgconfig')
subdir('tests')
subdir('po')

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

python3 = find_program('python3')
run_command(python3, '-c', 'import shutil; shutil.copy("hooks/pre-commit.hook", ".git/hooks/pre-commit")')
