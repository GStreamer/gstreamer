executable('gst-validate-' + apiversion,
           'gst-validate.c',
            install: true,
            include_directories : inc_dirs,
            dependencies : [gst_dep, glib_dep, gst_pbutils_dep, gio_dep],
            c_args : [gst_c_args],
            link_with : [gstvalidate]
          )
executable('gst-validate-transcoding-' + apiversion,
           'gst-validate-transcoding.c',            install: true,
            include_directories : inc_dirs,
            dependencies : [gst_dep, glib_dep, gst_pbutils_dep, gst_video_dep, gio_dep],
            c_args : [gst_c_args],
            link_with : [gstvalidate]
          )
executable('gst-validate-media-check-' + apiversion,
           'gst-validate-media-check.c',
            install: true,
            include_directories : inc_dirs,
            dependencies : [gst_dep, glib_dep, gst_pbutils_dep, gio_dep],
            c_args : [gst_c_args],
            link_with : [gstvalidate]
        )

rtsp_server_dep = dependency('gstreamer-rtsp-server-' + apiversion,
  fallback: ['gst-rtsp-server', 'gst_rtsp_server_dep'],
  required: false)

if rtsp_server_dep.found()
    executable('gst-validate-rtsp-server-' + apiversion,
               'gst-validate-rtsp-server.c',
               install: true,
               include_directories: inc_dirs,
               dependencies: [rtsp_server_dep],
               c_args: [gst_c_args],
               link_with: [gstvalidate]
    )
endif

if cairo_dep.found()
    executable('gst-validate-image-check-' + apiversion,
            'gst-validate-images-check.c',
                install: true,
                include_directories : inc_dirs,
                dependencies : [gst_dep, glib_dep, gst_pbutils_dep, gio_dep],
                c_args : [gst_c_args],
                link_with : [gstvalidate, video]
            )
endif

tmpconf = configuration_data()
tmpconf.set('LIBDIR', join_paths(get_option('prefix'), get_option('libdir')))
tmpconf.set('BUILDDIR', meson.current_build_dir())
tmpconf.set('SRCDIR', meson.current_source_dir())

configure_file(input : 'gst-validate-launcher.in',
               install_dir: get_option('bindir'),
               output : 'gst-validate-launcher',
               configuration : tmpconf)

launcher = find_program(meson.current_build_dir() + '/gst-validate-launcher')
