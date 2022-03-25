shared_library('gstvalidategtk', 'gstvalidategtk.c',
        include_directories : inc_dirs,
        dependencies : [gst_dep, gst_pbutils_dep, gtk_dep],
        c_args: ['-DHAVE_CONFIG_H'],
        install : true,
        install_dir : validate_plugins_install_dir,
        link_with : [gstvalidate])
