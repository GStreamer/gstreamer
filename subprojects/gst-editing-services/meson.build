project('gst-editing-services', 'c',
  version : '1.20.2.1',
  meson_version : '>= 0.59',
  default_options : [ 'warning_level=1',
                      'buildtype=debugoptimized' ])

gst_version = meson.project_version()
version_arr = gst_version.split('.')
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

apiversion = '1.0'
soversion = 0
# maintaining compatibility with the previous libtool versioning
# current = minor * 100 + micro
curversion = gst_version_minor * 100 + gst_version_micro
libversion = '@0@.@1@.0'.format(soversion, curversion)
osxversion = curversion + 1

glib_req = '>= 2.56.0'

if gst_version_is_stable
  gst_req = '>= @0@.@1@.0'.format(gst_version_major, gst_version_minor)
else
  gst_req = '>= ' + gst_version
endif

cc = meson.get_compiler('c')
mathlib = cc.find_library('m', required : false)

cdata = configuration_data()

prefix = get_option('prefix')
datadir = prefix / get_option('datadir')

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

cdata.set('VERSION', '"@0@"'.format(gst_version))
cdata.set('PACKAGE', '"gst-editing-services"')
cdata.set('PACKAGE_VERSION', '"@0@"'.format(gst_version))
cdata.set('PACKAGE_BUGREPORT', '"https://gitlab.freedesktop.org/gstreamer/gst-editing-services/issues/new"')
cdata.set('PACKAGE_NAME', '"GStreamer Editing Services"')
cdata.set('GST_PACKAGE_NAME', '"GStreamer Editing Services"')
cdata.set('GST_PACKAGE_ORIGIN', '"Unknown package origin"')
cdata.set('GST_LICENSE', '"LGPL"')

# Mandatory GST deps
gst_dep = dependency('gstreamer-' + apiversion, version : gst_req,
    fallback : ['gstreamer', 'gst_dep'])
gstpbutils_dep = dependency('gstreamer-pbutils-' + apiversion, version : gst_req,
    fallback : ['gst-plugins-base', 'pbutils_dep'])
gstvideo_dep = dependency('gstreamer-video-' + apiversion, version : gst_req,
    fallback : ['gst-plugins-base', 'video_dep'])
gstaudio_dep = dependency('gstreamer-audio-' + apiversion, version : gst_req,
    fallback : ['gst-plugins-base', 'audio_dep'])
gstbase_dep = dependency('gstreamer-base-1.0', version : gst_req,
    fallback : ['gstreamer', 'gst_base_dep'])
if host_machine.system() != 'windows'
  gstcheck_dep = dependency('gstreamer-check-1.0', version : gst_req,
    required : get_option('tests'),
    fallback : ['gstreamer', 'gst_check_dep'])
endif
gstcontroller_dep = dependency('gstreamer-controller-1.0', version : gst_req,
  fallback : ['gstreamer', 'gst_controller_dep'])
gstvalidate_dep = dependency('gst-validate-1.0', version : gst_req, required : get_option('validate'),
  fallback : ['gst-devtools', 'validate_dep'])

gio_dep = dependency('gio-2.0', version: glib_req, fallback: ['glib', 'libgio_dep'])
libxml_dep = dependency('libxml-2.0', required: get_option('xptv'))
cdata.set('DISABLE_XPTV', not libxml_dep.found())

# TODO Properly port to Gtk 3
# gtk_dep = dependency('gtk+-3.0', required : false)

libges_deps = [gst_dep, gstbase_dep, gstvideo_dep, gstpbutils_dep,
               gstcontroller_dep, gio_dep, libxml_dep, mathlib]

if gstvalidate_dep.found()
    libges_deps = libges_deps + [gstvalidate_dep]
    cdata.set('HAVE_GST_VALIDATE', 1)
endif

gir = find_program('g-ir-scanner', required : get_option('introspection'))
gnome = import('gnome')

# Fixme, not very elegant.
build_gir = gir.found() and (not meson.is_cross_build() or get_option('introspection').enabled())
gir_init_section = [ '--add-init-section=' + \
    'extern void gst_init(gint*,gchar**);' + \
    'extern void ges_init(void);' + \
    'g_setenv("GST_REGISTRY_1.0", "/no/way/this/exists.reg", TRUE);' + \
    'g_setenv("GST_PLUGIN_PATH_1_0", "", TRUE);' + \
    'g_setenv("GST_PLUGIN_SYSTEM_PATH_1_0", "", TRUE);' + \
    'g_setenv("GST_DEBUG", "0", TRUE);' + \
    'gst_init(NULL,NULL);' + \
    'ges_init();', '--quiet']

has_python = false
static_build = get_option('default_library') == 'static'
if static_build
  if get_option('python').enabled()
    error('Want to build python based modules but it is not supported while static building')
  else
    message('Disabling python support as it is not supported on static builds')
  endif
elif build_gir
  pymod = import('python')
  python = pymod.find_installation(required: get_option('python'))
  if python.found()
    # Workaround for https://github.com/mesonbuild/meson/issues/5629
    pythonver = python.language_version()
    python_dep = dependency('python-@0@-embed'.format(pythonver), version: '>=3', required: false)
    if not python_dep.found()
      python_dep = python.dependency(required : get_option('python'))
    endif
  else
    python_dep = dependency('', required: false)
  endif
  if python_dep.found()
    python_abi_flags = python.get_variable('ABIFLAGS', '')
    pylib_loc = get_option('libpython-dir')

    error_msg = ''
    if not cc.compiles('#include <Python.h>', dependencies: [python_dep])
      error_msg = 'Could not compile a simple program against python'
    elif pylib_loc == ''
      fsmod = import('fs')
      pylib_loc = python.get_variable('LIBPL', '')
      if host_machine.system() != 'windows' and host_machine.system() != 'darwin'
        pylib_ldlibrary = python.get_variable('LDLIBRARY', '')
        if not fsmod.exists(pylib_loc / pylib_ldlibrary)
          # Workaround for Fedora
          pylib_loc = python.get_variable('LIBDIR', '')
          message('pylib_loc = @0@'.format(pylib_loc))
        endif

        if not fsmod.exists(pylib_loc / pylib_ldlibrary)
          error_msg = '@0@ doesn\' exist, can\'t use python'.format(join_paths(pylib_loc, pylib_ldlibrary))
        endif
      endif
      if error_msg == ''
        pylib_suffix = 'so'
        if host_machine.system() == 'windows'
          pylib_suffix = 'dll'
        elif host_machine.system() == 'darwin'
          pylib_suffix = 'dylib'
        endif

        gmodule_dep = dependency('gmodule-2.0')
        libges_deps = libges_deps + [python_dep, gmodule_dep]
        has_python = true
        message('python_abi_flags = @0@'.format(python_abi_flags))
        message('pylib_loc = @0@'.format(pylib_loc))
        cdata.set('HAS_PYTHON', true)
        cdata.set('PY_LIB_LOC', '"@0@"'.format(pylib_loc))
        cdata.set('PY_ABI_FLAGS', '"@0@"'.format(python_abi_flags))
        cdata.set('PY_LIB_SUFFIX', '"@0@"'.format(pylib_suffix))
        cdata.set('PYTHON_VERSION', '"@0@"'.format(python_dep.version()))
      else
          if get_option('python').enabled()
            error(error_msg)
          else
            message(error_msg)
          endif
      endif
    endif
  endif
endif

ges_c_args = ['-DHAVE_CONFIG_H', '-DG_LOG_DOMAIN="GES"']
plugins_install_dir = '@0@/gstreamer-1.0'.format(get_option('libdir'))

pkgconfig = import('pkgconfig')
plugins_pkgconfig_install_dir = join_paths(plugins_install_dir, 'pkgconfig')
if get_option('default_library') == 'shared'
  # If we don't build static plugins there is no need to generate pc files
  plugins_pkgconfig_install_dir = disabler()
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

warning_flags = [
  '-Wmissing-declarations',
  '-Wmissing-prototypes',
  '-Wredundant-decls',
  '-Wundef',
  '-Wwrite-strings',
  '-Wformat',
  '-Wformat-security',
  '-Winit-self',
  '-Wmissing-include-dirs',
  '-Waddress',
  '-Wno-multichar',
  '-Wdeclaration-after-statement',
  '-Wvla',
  '-Wpointer-arith',
]

foreach extra_arg : warning_flags
  if cc.has_argument (extra_arg)
    add_project_arguments([extra_arg], language: 'c')
  endif
endforeach

python3 = import('python').find_installation()
pkgconfig = import('pkgconfig')
pkgconfig_subdirs = ['gstreamer-1.0']

configinc = include_directories('.')
libraries = []

subdir('ges')
subdir('plugins')
if not get_option('tools').disabled()
  subdir('tools')
endif
subdir('tests')
if not get_option('examples').disabled()
  subdir('examples')
endif
subdir('docs')

override_detector = '''
import sys
import os

prefix = sys.argv[1]
version = sys.version_info

# If we are installing in the same prefix as PyGobject
# make sure to install in the right place.
import gi.overrides

overrides_path = os.path.dirname(gi.overrides.__file__)
if os.path.commonprefix([overrides_path, prefix]) == prefix:
    print(overrides_path)
    exit(0)

# Otherwise follow python's way of install site packages inside
# the provided prefix
if os.name == 'posix':
    print(os.path.join(
        prefix, 'lib', 'python%d.%d' % (version.major, version.minor),
        'site-packages', 'gi', 'overrides'))
else:
    print(os.path.join(
        prefix, 'Lib', 'Python%d%d' % (version.major, version.minor),
        'site-packages', 'gi', 'overrides'))
'''
pygi_override_dir = get_option('pygi-overrides-dir')
if pygi_override_dir == ''
    cres = run_command(python3, '-c', override_detector, get_option('prefix'), check: false)
    if cres.returncode() == 0
      pygi_override_dir = cres.stdout().strip()
    endif
    if cres.stderr() != ''
        message(cres.stderr())
    endif
endif

if pygi_override_dir != ''
  message('pygobject overrides directory ' + pygi_override_dir)
  subdir('bindings/python')
endif

# Set release date
if gst_version_nano == 0
  extract_release_date = find_program('scripts/extract-release-date-from-doap-file.py')
  run_result = run_command(extract_release_date, gst_version, files('gst-editing-services.doap'), check: true)
  release_date = run_result.stdout().strip()
  cdata.set_quoted('GST_PACKAGE_RELEASE_DATETIME', release_date)
  message('Package release date: ' + release_date)
endif

if gio_dep.version().version_compare('< 2.67.4')
  cdata.set('g_memdup2(ptr,sz)', '(G_LIKELY(((guint64)(sz)) < G_MAXUINT)) ? g_memdup(ptr,sz) : (g_abort(),NULL)')
endif

configure_file(output: 'config.h', configuration: cdata)
