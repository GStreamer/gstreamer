if validate_video_dep.found()
    shared_library('gstvalidatessim',
                'gstvalidatessim.c',
                include_directories : inc_dirs,
                link_with: [gstvalidate],
                dependencies : [gst_dep, gst_video_dep, validate_video_dep,
                    gst_pbutils_dep],
                install : true,
                install_dir : validate_plugins_install_dir,
                c_args: ['-DHAVE_CONFIG_H'],
                )
endif
