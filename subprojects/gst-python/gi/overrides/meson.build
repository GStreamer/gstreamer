pysources = ['Gst.py', 'GstPbutils.py', 'GstVideo.py', 'GstAudio.py']
python.install_sources(pysources,
  pure : false,
  subdir: 'gi/overrides',
)

host_system = host_machine.system()
if host_system == 'windows'
  gst_dep_for_gi = gst_dep
else
  gst_dep_for_gi = gst_dep.partial_dependency(compile_args: true, includes: true, sources: true)
endif

gstpython = python.extension_module('_gi_gst',
    sources: ['gstmodule.c'],
    install: true,
    install_dir : pygi_override_dir,
    install_tag: 'python-runtime',
    include_directories : [configinc],
    dependencies : [gst_dep_for_gi, python_dep, pygobject_dep],
)

env = environment()
env.prepend('_GI_OVERRIDES_PATH', [
    meson.current_source_dir(),
    meson.current_build_dir()
])
meson.add_devenv(env)
