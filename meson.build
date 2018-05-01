project('gst-python', 'c', 'cpp',
  version : '1.15.0.1',
  meson_version : '>= 0.40.0',
  default_options : [ 'warning_level=1',
                      'c_std=gnu99',
                      'buildtype=debugoptimized' ])

gst_version = meson.project_version()
version_arr = gst_version.split('.')
gst_version_major = version_arr[0]
gst_version_minor = version_arr[1]
api_version = '@0@.0'.format(gst_version_major)

gst_req = '>= @0@.@1@.0'.format(gst_version_major, gst_version_minor)

gst_dep = dependency('gstreamer-1.0', version : gst_req,
  fallback : ['gstreamer', 'gst_dep'])
gstbase_dep = dependency('gstreamer-base-1.0', version : gst_req,
  fallback : ['gstreamer', 'gst_base_dep'])
gmodule_dep = dependency('gmodule-2.0')
pygobject_dep = dependency('pygobject-3.0', version : '>= 3.8')
python_dep = dependency('python3')

python3 = import('python3').find_python()
pythondetector = find_program('scripts/pythondetector')

cres = run_command(pythondetector, '--sosuffix')
if cres.returncode() != 0
    error('Could not detect python sosuffix' + cres.stdout() + cres.stderr())
endif
py_so_suffix = cres.stdout().strip()

cres = run_command(pythondetector, '--abiflags')
if cres.returncode() != 0
    error('Could not detect python abiflags' + cres.stdout() + cres.stderr())
endif
python_abi_flags = cres.stdout().strip()

cres = run_command(pythondetector, '--libloc')
if cres.returncode() != 0
    error('Could not detect python library location' + cres.stdout() + cres.stderr())
endif
pylib_loc = cres.stdout().strip()

assert(pylib_loc != 'None', 'Python dynamic library path could not be determined')
pygi_override_dir = get_option('pygi-overrides-dir')
if pygi_override_dir == ''
    cres = run_command(pythondetector, '--pygi-overridedir',
            get_option('prefix'))
    if cres.returncode() != 0
        error('Could not detect PyGObject overrides location' + cres.stdout() + cres.stderr())
    endif
    pygi_override_dir = cres.stdout().strip()
    if cres.stderr() != ''
        message(cres.stderr())
    endif
endif
message('pygobject overrides directory ' + pygi_override_dir)

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
cdata.set('PLUGINDIR', '"@0@/gstreamer-1.0"'.format(get_option('libdir')))
cdata.set('PY_LIB_LOC', '"@0@"'.format(pylib_loc))
cdata.set('PY_ABI_FLAGS', '"@0@"'.format(python_abi_flags))
cdata.set('PY_LIB_SUFFIX', '"@0@"'.format(pylib_suffix))
cdata.set('PYTHON_VERSION', '"@0@"'.format(python_dep.version()))
configure_file(output : 'config.h', configuration : cdata)
configinc = include_directories('.')

subdir('gi')
subdir('plugin')
subdir('testsuite')

run_command(python3, '-c', 'import shutil; shutil.copy("hooks/pre-commit.hook", ".git/hooks/pre-commit")')
