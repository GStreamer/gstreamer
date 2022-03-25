shared_library('gstvalidategapplication',
               'gstvalidategapplication.c',
                include_directories : inc_dirs,
                c_args: ['-DHAVE_CONFIG_H'],
                install: true,
                install_dir: validate_plugins_install_dir,
                dependencies : [gst_dep, gst_pbutils_dep, gio_dep],
                link_with : [gstvalidate]
               )
