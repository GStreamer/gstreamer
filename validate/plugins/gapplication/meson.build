shared_library('gstvalidategapplication',
               'gstvalidategapplication.c',
                install: true,
                install_dir: PLUGINDIR,
                include_directories : inc_dirs,
                dependencies : [gst_dep, glib_dep, gst_pbutils_dep],
                link_with : [gstvalidate]
               )
