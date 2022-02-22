gst_python = subproject('gst-python', required: false)
gir = find_program('g-ir-scanner', required : get_option('introspection'))
if not gst_python.found() or not gir.found()
    message('Not running python devenv tests: gst_python: @0@ gir: @1@'.format(gst_python.found(), gir.found()))
    subdir_done()
endif

root_rel = '../..'
python = import('python').find_installation()

if run_command(python, '-c', 'import gi', check: false).returncode() != 0
    message('PyGObject not found, not running PyGObject tests')
    subdir_done()
endif

test('python-overrides-devenv', setenv, args: ['--builddir=@0@'.format(meson.project_build_root()),
              '--srcdir=@0@'.format(meson.project_source_root()),
              meson.current_source_dir() / 'python-devenv-overrides.py'])

env = environment()
env.set('GI_TYPELIB_PATH', meson.current_build_dir() / root_rel)
if build_machine.system() == 'windows'
    env.append('PATH', meson.current_build_dir() / root_rel)
elif build_machine.system() == 'linux'
    env.append('LD_LIBRARY_PATH', meson.current_build_dir() / root_rel)
else
    env.append('DYLD_LIBRARY_PATH', meson.current_build_dir() / root_rel)
endif

env.set('GST_OVERRIDE_SRC_PATH', meson.current_source_dir() / root_rel / 'subprojects/gst-python/gi/overrides')
env.set('GST_OVERRIDE_BUILD_PATH', meson.current_build_dir() / root_rel / 'subprojects/gst-python/gi/overrides')

test('python-full', python, args: [meson.current_source_dir() / 'python-full.py'], env: env)
