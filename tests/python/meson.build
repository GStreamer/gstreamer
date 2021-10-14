gst_python = subproject('gst-python', required: false)
gir = find_program('g-ir-scanner', required : get_option('introspection'))
if not gst_python.found() or not gir.found()
    message('Not running python devenv tests: gst_python: @0@ gir: @1@'.format(gst_python.found(), gir.found()))
    subdir_done()
endif

root_rel = '../..'
python = import('python').find_installation()

if run_command(python, '-c', 'import gi').returncode() != 0
    message('PyGObject not found, not running PyGObject tests')
    subdir_done()
endif

test('python-overrides-devenv', setenv, args: ['--builddir=@0@'.format(meson.build_root()),
              '--gstbuilddir=@0@'.format(meson.current_build_dir() / '..' / '..'),
              '--srcdir=@0@'.format(meson.source_root()),
              meson.current_source_dir() / 'python-devenv-overrides.py'])