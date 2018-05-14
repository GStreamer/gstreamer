pysources = ['Gst.py', 'GstPbutils.py']
install_data(pysources,
    install_dir: pygi_override_dir)

gstpython = python.extension_module('_gi_gst',
    sources: ['gstmodule.c'],
    install: true,
    install_dir : pygi_override_dir,
    include_directories : [configinc],
    dependencies : [gst_dep, python_dep, pygobject_dep])

gi_overrides_build_dir = meson.current_build_dir()

# Workaround to get uninstalled working.
foreach source: pysources
    run_command(python, '-c', 'import os; os.symlink("@0@/@1@", "@2@/@3@")'.format(
        meson.current_source_dir(), source,
        gi_overrides_build_dir, source))
endforeach
