gstpython = library('gstpython',
    ['gstpythonplugin.c'],
    include_directories : [configinc],
    dependencies : [gst_dep, pygobject_dep, gstbase_dep, python_dep, gmodule_dep] ,
    install : true,
    install_dir : '@0@/gstreamer-1.0'.format(get_option('libdir')),
)
pkgconfig.generate(gstpython, install_dir : plugins_pkgconfig_install_dir)
plugins = [gstpython]
