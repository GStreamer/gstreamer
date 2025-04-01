gstpython = library('gstpython',
    ['gstpythonplugin.c'],
    include_directories : [configinc],
    dependencies : [gst_dep, pygobject_dep, gstbase_dep, python_embed_dep, gmodule_dep, libdl],
    install : true,
    install_dir : '@0@/gstreamer-1.0'.format(get_option('libdir')),
)
plugins = [gstpython]
# XXX: Generate a pc file for this plugin? Can gstpython be statically linked?
dep = declare_dependency(link_with: gstpython, variables: {'full_path': gstpython.full_path()})
meson.override_dependency(gstpython.name(), dep)
gst_plugins = [dep]
