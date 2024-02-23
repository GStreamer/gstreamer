project('gst-python', 'c',
  version : '1.23.90',
  meson_version : '>= 1.1',
  default_options : [ 'warning_level=1',
                      'c_std=gnu99',
                      'buildtype=debugoptimized' ])

gst_version = meson.project_version()
version_arr = gst_version.split('.')
gst_version_major = version_arr[0]
gst_version_minor = version_arr[1]
api_version = '@0@.0'.format(gst_version_major)
host_system = host_machine.system()
cc = meson.get_compiler('c')

add_project_arguments('-DHAVE_CONFIG_H', language: 'c')

gst_req = '>= @0@.@1@.0'.format(gst_version_major, gst_version_minor)

gst_dep = dependency('gstreamer-1.0', version : gst_req,
  fallback : ['gstreamer', 'gst_dep'])
gstbase_dep = dependency('gstreamer-base-1.0', version : gst_req,
  fallback : ['gstreamer', 'gst_base_dep'])
gmodule_dep = dependency('gmodule-no-export-2.0')
libdl = cc.find_library('dl', required: false)
pygobject_dep = dependency('pygobject-3.0', fallback: ['pygobject', 'pygobject_dep'], version : '>= 3.8')

pymod = import('python')
python = pymod.find_installation(get_option('python'))
pythonver = python.language_version()
if pythonver.version_compare('<3.0')
  error('Python2 is not supported anymore, please port your code to python3 (@0@ specified)'.format(python.language_version()))
endif
if pythonver.version_compare('<3.7')
  error('Could not find a sufficient python version required: 3.7, found @0@'.format(python.language_version()))
endif


python_dep = python.dependency(embed: true, required: true)

python_abi_flags = python.get_variable('ABIFLAGS', '')
message(f'python_abi_flags = @python_abi_flags@')
pylib_loc = get_option('libpython-dir')
fsmod = import('fs')
pylib_prefix = 'lib'
pylib_suffix = 'so'
if host_system == 'windows'
  if cc.get_argument_syntax() == 'msvc'
    pylib_prefix = ''
  endif
  pylib_suffix = 'dll'
elif host_system == 'darwin'
  pylib_suffix = 'dylib'
endif
pylib_fnames = []
# Library name with soversion, non-devel package
pylib_fnames += python.get_variable('INSTSONAME', [])
# Library name without soversion, devel package, framework, etc.
pylib_fnames += python.get_variable('LDLIBRARY', [])
# Manually construct name as a fallback
pylib_fnames += [
  pylib_prefix + 'python' + python_dep.version() + python_abi_flags + '.' + pylib_suffix
]
if pylib_loc != ''
  pylib_locs = [pylib_loc]
else
  pylib_locs = [
    python.get_variable('LIBDIR', ''),
    python.get_variable('LIBPL', ''),
  ]
endif
pylib_fname = ''
foreach loc: pylib_locs
  foreach fname: pylib_fnames
    if fsmod.exists(loc / fname)
      pylib_fname = fname
      message(f'PY_LIB_FNAME = @fname@ (@loc@)')
      break
    endif
  endforeach
endforeach
if pylib_fname == ''
  error_msg = 'Could not find python library to load'
  if python_opt.enabled()
    error(error_msg)
  else
    message(error_msg)
  endif
endif

pygi_override_dir = get_option('pygi-overrides-dir')

if pygi_override_dir == ''
  pygi_override_dir = python.get_install_dir(
    subdir : join_paths('gi', 'overrides'),
    pure: false
  )
endif

message('pygobject overrides directory = @0@'.format(pygi_override_dir))

# libdir has to be built from pieces.
libdir = get_option('prefix')+'/'+get_option('libdir')

cdata = configuration_data()
cdata.set_quoted('PACKAGE', 'gst-python')
cdata.set_quoted('VERSION', gst_version)
cdata.set_quoted('GST_PACKAGE_NAME', 'GStreamer Python')
cdata.set_quoted('PACKAGE_NAME', 'GStreamer Python')
cdata.set_quoted('GST_API_VERSION', api_version)
cdata.set_quoted('PLUGINDIR', join_paths(get_option('prefix'), get_option('libdir'), 'gstreamer-1.0'))
cdata.set_quoted('PY_LIB_FNAME', pylib_fname)
configure_file(output : 'config.h', configuration : cdata)
configinc = include_directories('.')

meson.add_dist_script('scripts/gen-changelog.py', meson.project_name(), '1.22.0', meson.project_version())

pkgconfig = import('pkgconfig')
plugins_install_dir = join_paths(libdir, 'gstreamer-1.0')
plugins_pkgconfig_install_dir = join_paths(plugins_install_dir, 'pkgconfig')
if get_option('default_library') == 'shared'
  # If we don't build static plugins there is no need to generate pc files
  plugins_pkgconfig_install_dir = disabler()
endif

subdir('gi')
if not get_option('plugin').disabled()
  if get_option('default_library') != 'static'
    subdir('plugin')
  else
    warning('Python plugin not supported with `static` builds yet.')
  endif
endif
if not get_option('tests').disabled()
  subdir('testsuite')
endif
