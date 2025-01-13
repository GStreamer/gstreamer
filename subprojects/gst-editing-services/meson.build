project('gst-editing-services', 'c',
  version : '1.25.0.1',
  meson_version : '>= 1.3',
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

api_version = '1.0'
soversion = 0
# maintaining compatibility with the previous libtool versioning
# current = minor * 100 + micro
curversion = gst_version_minor * 100 + gst_version_micro
libversion = '@0@.@1@.0'.format(soversion, curversion)
osxversion = curversion + 1

glib_req = '>= 2.64.0'

if gst_version_is_stable
  gst_req = '>= @0@.@1@.0'.format(gst_version_major, gst_version_minor)
else
  gst_req = '>= ' + gst_version
endif

host_system = host_machine.system()
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
  ]
  add_project_arguments(msvc_args, language: 'c')
endif

if cc.has_link_argument('-Wl,-Bsymbolic-functions')
  add_project_link_arguments('-Wl,-Bsymbolic-functions', language : 'c')
endif

# glib doesn't support unloading, which means that unloading and reloading
# any library that registers static types will fail
if cc.has_link_argument('-Wl,-z,nodelete')
  add_project_link_arguments('-Wl,-z,nodelete', language: 'c')
endif

# Symbol visibility
if cc.has_argument('-fvisibility=hidden')
  add_project_arguments('-fvisibility=hidden', language: 'c')
endif

# Disable strict aliasing
if cc.has_argument('-fno-strict-aliasing')
  add_project_arguments('-fno-strict-aliasing', language: 'c')
endif

cdata.set_quoted('VERSION', gst_version)
cdata.set_quoted('PACKAGE', 'gst-editing-services')
cdata.set_quoted('PACKAGE_VERSION', gst_version)
cdata.set_quoted('PACKAGE_BUGREPORT', 'https://gitlab.freedesktop.org/gstreamer/gstreamer/issues/new')
cdata.set_quoted('PACKAGE_NAME', 'GStreamer Editing Services')
cdata.set_quoted('GST_PACKAGE_NAME', 'GStreamer Editing Services')
cdata.set_quoted('GST_PACKAGE_ORIGIN', 'Unknown package origin')
cdata.set_quoted('GST_LICENSE', 'LGPL')

# Mandatory GST deps
gst_dep = dependency('gstreamer-' + api_version, version : gst_req,
    fallback : ['gstreamer', 'gst_dep'])
gstpbutils_dep = dependency('gstreamer-pbutils-' + api_version, version : gst_req,
    fallback : ['gst-plugins-base', 'pbutils_dep'])
gstvideo_dep = dependency('gstreamer-video-' + api_version, version : gst_req,
    fallback : ['gst-plugins-base', 'video_dep'])
gstaudio_dep = dependency('gstreamer-audio-' + api_version, version : gst_req,
    fallback : ['gst-plugins-base', 'audio_dep'])
gstbase_dep = dependency('gstreamer-base-1.0', version : gst_req,
    fallback : ['gstreamer', 'gst_base_dep'])
if host_system != 'windows'
  gstcheck_dep = dependency('gstreamer-check-1.0', version : gst_req,
    required : get_option('tests'),
    fallback : ['gstreamer', 'gst_check_dep'])
endif
gstcontroller_dep = dependency('gstreamer-controller-1.0', version : gst_req,
  fallback : ['gstreamer', 'gst_controller_dep'])
gstvalidate_dep = dependency('gstreamer-validate-1.0', version : gst_req, required : get_option('validate'),
  fallback : ['gst-devtools', 'validate_dep'])

gio_dep = dependency('gio-2.0', version: glib_req)
gmodule_dep = dependency('gmodule-no-export-2.0')
libdl = cc.find_library('dl', required: false)
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
    'g_setenv("GST_TRACERS", "", TRUE);' + \
    'gst_init(NULL,NULL);' + \
    'ges_init();', '--quiet']

pymod = import('python')
python_opt = get_option('python')
python = pymod.find_installation(required: python_opt)
has_python = false
static_build = get_option('default_library') == 'static'
if static_build
  if python_opt.enabled()
    error('Want to build python based modules but it is not supported while static building')
  else
    message('Disabling python support as it is not supported on static builds')
  endif
elif build_gir and python.found()
  python_dep = python.dependency(required : python_opt, embed: true,
                                 include_type: 'system')
  if python_dep.found()
    python_abi_flags = python.get_variable('ABIFLAGS', '')
    message(f'python_abi_flags = @python_abi_flags@')
    pylib_loc = get_option('libpython-dir')

    error_msg = ''
    if not cc.compiles('#include <Python.h>', dependencies: [python_dep])
      error_msg = 'Could not compile a simple program against python'
      if python_opt.enabled()
        error(error_msg)
      else
        message(error_msg)
      endif
    endif
    fsmod = import('fs')
    pylib_prefix = 'lib'
    pylib_suffix = 'so'
    pylib_ver = python_dep.version()
    pylib_locs = []
    if host_system == 'windows'
      if cc.get_argument_syntax() == 'msvc'
        pylib_prefix = ''
      endif
      pylib_suffix = 'dll'
      pylib_ver = pylib_ver.replace('.', '')
    elif host_system == 'darwin'
      pylib_suffix = 'dylib'
    endif
    pylib_fnames = []
    if host_system != 'darwin' # Ignore system Python (INSTSONAME points to the Python3 executable and not libpython3.9.dylib)
      # Library name with soversion, non-devel package
      if python.has_variable('INSTSONAME')
        pylib_fnames += python.get_variable('INSTSONAME')
      endif
      # Library name without soversion, devel package, framework, etc.
      if python.has_variable('LDLIBRARY')
        pylib_fnames += python.get_variable('LDLIBRARY')
      endif
    endif
    # Manually construct name as a fallback
    pylib_fnames += [
      pylib_prefix + 'python' + pylib_ver + python_abi_flags + '.' + pylib_suffix
    ]
    if pylib_loc != ''
      pylib_locs = [pylib_loc]
    else
      if python.has_variable('LIBDIR')
        pylib_locs += python.get_variable('LIBDIR')
      endif
      if python.has_variable('LIBPL')
        pylib_locs += python.get_variable('LIBPL')
      endif
      # On Windows, python312.dll is in the rootdir where Python is installed,
      # which is configured as the "prefix" in sysconfig.
      if host_system == 'windows'
        pylib_locs += python.get_variable('prefix')
      endif
    endif
    foreach loc: pylib_locs
      foreach fname: pylib_fnames
        fpath = loc / fname
        debug(f'Looking for Python library at: @fpath@')
        if fsmod.exists(fpath)
          libges_deps = libges_deps + [python_dep, gmodule_dep, libdl]
          has_python = true
          cdata.set('HAS_PYTHON', true)
          cdata.set_quoted('PY_LIB_FNAME', fname)
          message(f'PY_LIB_FNAME = @fname@ (@loc@)')
          break
        endif
      endforeach
    endforeach
    if not has_python
      message('Could not find python library to load, will try loading at runtime')
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

if gst_debug_disabled
  message('GStreamer debug system is disabled')
  if cc.get_argument_syntax() == 'msvc'
    msvc_args = cc.get_supported_arguments([
      '/wd4101', # 'identifier' : unreferenced local variable
      '/wd4189', # 'identifier' : local variable is initialized but not referenced
    ])
    add_project_arguments(msvc_args, language: 'c')
  elif cc.has_argument('-Wno-unused')
    add_project_arguments(['-Wno-unused'], language: 'c')
  endif
else
  if cc.get_argument_syntax() == 'msvc' and gst_version_is_dev
    msvc_args = cc.get_supported_arguments([
      '/we4101', # 'identifier' : unreferenced local variable
      '/we4189', # 'identifier' : local variable is initialized but not referenced
    ])
    add_project_arguments(msvc_args, language: 'c')
  endif
  message('GStreamer debug system is enabled')
endif

# NOTE: Keep entries alphabetically sorted
warning_flags = [
  '-Waddress',
  # '-Waggregate-return',
  '-Wformat',
  '-Wformat-security',
  '-Wimplicit-fallthrough=3',
  '-Winit-self',
  '-Wmissing-declarations',
  '-Wmissing-include-dirs',
  '-Wmissing-parameter-type',
  '-Wmissing-prototypes',
  '-Wno-multichar',
  '-Wold-style-definition',
  '-Wpointer-arith',
  '-Wredundant-decls',
  '-Wshift-negative-value',
  '-Wtype-limits',
  '-Wundef',
  '-Wvla',
  '-Wwrite-strings',
]

foreach extra_arg : warning_flags
  if cc.has_argument (extra_arg)
    add_project_arguments([extra_arg], language: 'c')
  endif
endforeach

pkgconfig = import('pkgconfig')
pkgconfig_subdirs = ['gstreamer-1.0']

configinc = include_directories('.')
gst_libraries = []

if host_system == 'windows'
  pathsep = ';'
else
  pathsep = ':'
endif

subdir('ges')
subdir('plugins')
subdir('tools')

subdir('tests')
subdir('examples')
subdir('docs')

pygi_override_dir = get_option('pygi-overrides-dir')
if pygi_override_dir == '' and python.found()
  pygi_override_dir = python.get_install_dir(
    subdir : join_paths('gi', 'overrides')
  )
endif

if python.found()
  message('pygobject overrides directory = @0@'.format(pygi_override_dir))
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

meson.add_dist_script('scripts/gen-changelog.py', meson.project_name(), '1.22.0', meson.project_version())

plugin_names = []
gst_plugins = []
foreach plugin: plugins
  pkgconfig.generate(plugin, install_dir: plugins_pkgconfig_install_dir)
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
