project('gst-python', 'c',
  version : '1.23.0.1',
  meson_version : '>= 0.62',
  default_options : [ 'warning_level=1',
                      'c_std=gnu99',
                      'buildtype=debugoptimized' ])

gst_version = meson.project_version()
version_arr = gst_version.split('.')
gst_version_major = version_arr[0]
gst_version_minor = version_arr[1]
api_version = '@0@.0'.format(gst_version_major)

add_project_arguments('-DHAVE_CONFIG_H', language: 'c')

gst_req = '>= @0@.@1@.0'.format(gst_version_major, gst_version_minor)

gst_dep = dependency('gstreamer-1.0', version : gst_req,
  fallback : ['gstreamer', 'gst_dep'])
gstbase_dep = dependency('gstreamer-base-1.0', version : gst_req,
  fallback : ['gstreamer', 'gst_base_dep'])
gmodule_dep = dependency('gmodule-no-export-2.0')
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


python_dep = python.dependency(embed:true, required : true)

python_abi_flags = python.get_variable('ABIFLAGS', '')
pylib_loc = get_option('libpython-dir')
if pylib_loc == ''
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
      error('Python dynamic library path could not be determined')
    endif
  endif
endif

message('python_abi_flags = @0@'.format(python_abi_flags))
message('pylib_loc = @0@'.format(pylib_loc))

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


pylib_suffix = 'so'
if host_machine.system() == 'windows'
  pylib_suffix = 'dll'
elif host_machine.system() == 'darwin'
  pylib_suffix = 'dylib'
endif
cdata = configuration_data()
cdata.set('PACKAGE', '"gst-python"')
cdata.set('VERSION', '"@0@"'.format(gst_version))
cdata.set('GST_PACKAGE_NAME', '"GStreamer Python"')
cdata.set('PACKAGE_NAME', '"GStreamer Python"')
cdata.set('GST_API_VERSION', '"@0@"'.format(api_version))
cdata.set('PLUGINDIR', '"@0@/gstreamer-1.0"'.format(libdir))
cdata.set('PY_LIB_LOC', '"@0@"'.format(pylib_loc))
cdata.set('PY_ABI_FLAGS', '"@0@"'.format(python_abi_flags))
cdata.set('PY_LIB_SUFFIX', '"@0@"'.format(pylib_suffix))
cdata.set('PYTHON_VERSION', '"@0@"'.format(python_dep.version()))
configure_file(output : 'config.h', configuration : cdata)
configinc = include_directories('.')

meson.add_dist_script('scripts/gen-changelog.py', meson.project_name(), '1.20.0', meson.project_version())

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
