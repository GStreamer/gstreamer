executable('gst-validate-' + apiversion,
           'gst-validate.c',
            install: true,
            include_directories : inc_dirs,
            dependencies : validate_dep,
            c_args : [gst_c_args],
          )

gst_transcoder_dep = dependency('gstreamer-transcoder-' + apiversion, version : gst_req,
    fallback : ['gst-plugins-bad', 'gst_transcoder_dep'], required: false)

if gst_transcoder_dep.found()
  executable('gst-validate-transcoding-' + apiversion,
             'gst-validate-transcoding.c',
             install: true,
             include_directories : inc_dirs,
             dependencies : [validate_dep, gst_transcoder_dep],
             c_args : [gst_c_args],
            )
else
  warning('Can not build gst-validate-transcoding-' + apiversion)
endif

executable('gst-validate-media-check-' + apiversion,
           'gst-validate-media-check.c',
            install: true,
            include_directories : inc_dirs,
            dependencies : validate_dep,
            c_args : [gst_c_args],
        )

rtsp_server_dep = dependency('gstreamer-rtsp-server-' + apiversion,
  fallback: ['gst-rtsp-server', 'gst_rtsp_server_dep'],
  version : gst_req,
  required: false)

if rtsp_server_dep.found()
    executable('gst-validate-rtsp-server-' + apiversion,
               'gst-validate-rtsp-server.c',
               install: true,
               include_directories: inc_dirs,
               dependencies : [rtsp_server_dep, validate_dep],
               c_args: [gst_c_args],
    )
endif

if validate_video_dep.found()
    executable('gst-validate-images-check-' + apiversion,
               'gst-validate-images-check.c',
                install: true,
                include_directories : inc_dirs,
                dependencies : [validate_dep, validate_video_dep],
                c_args : [gst_c_args]
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
