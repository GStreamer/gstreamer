if dl.found()
    shared_library('gstvalidatefaultinjection',
                    'socket_interposer.c',
                    include_directories : inc_dirs,
                    link_with: [gstvalidate],
                    dependencies : [gst_dep, dl],
                    c_args: ['-DHAVE_CONFIG_H'],
                    install : true,
                    install_dir : validate_plugins_install_dir,
                )
endif
