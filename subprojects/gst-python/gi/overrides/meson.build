pysources = ['Gst.py', 'GstPbutils.py', 'GstVideo.py', 'GstAudio.py']
install_data(pysources,
    install_dir: pygi_override_dir)

gstpython = python.extension_module('_gi_gst',
    sources: ['gstmodule.c'],
    install: true,
    install_dir : pygi_override_dir,
    include_directories : [configinc],
    dependencies : [gst_dep, python_dep, pygobject_dep])

env = environment()
env.prepend('_GI_OVERRIDES_PATH', [
    meson.current_source_dir(),
    meson.current_build_dir()
])
meson.add_devenv(env)
