gst_elements_shared = shared_library('gstpythonplugin',
    ['gstpythonplugin.c'],
    c_args : '-DHAVE_CONFIG_H -DPY_LIB_LOC=@0@'.format(pylib_loc),
    include_directories : [configinc],
    dependencies : [gst_dep, pygobject_dep, gstbase_dep, python_dep, gmodule_dep],
    install : true,
    install_dir : '@0@/gstreamer-1.0'.format(get_option('libdir')),
)
