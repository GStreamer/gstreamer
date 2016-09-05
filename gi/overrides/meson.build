pysources = ['Gst.py', 'GstPbutils.py']
install_data(pysources,
    install_dir: pygi_override_dir)

gstpython = shared_library('_gi_gst',
    sources: ['gstmodule.c'],
    name_prefix: '',
    name_suffix: py_so_suffix,
    install: true,
    install_dir : pygi_override_dir,
    dependencies : [gst_dep, python_dep, pygobject_dep])

# Workaround to get uninstalled working.
foreach source: pysources
    run_command(python, '-c', 'import os; os.symlink("@0@/@1@", "@2@/@3@")'.format(
        meson.current_source_dir(), source,
        meson.current_build_dir(), source))
endforeach
