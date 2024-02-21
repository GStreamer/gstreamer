gstpython = library('gstpython',
    ['gstpythonplugin.c'],
    include_directories : [configinc],
    dependencies : [gst_dep, pygobject_dep, gstbase_dep, python_dep, gmodule_dep, libdl],
    install : true,
    install_dir : '@0@/gstreamer-1.0'.format(get_option('libdir')),
)
plugins = [gstpython]
