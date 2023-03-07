gst_tools = {
  'gst-validate': {
    'files': files('gst-validate.c'),
    'deps': [validate_dep, gio_dep],
  },
  'gst-validate-media-check': {
    'files': files('gst-validate-media-check.c'),
    'deps': [validate_dep, gio_dep],
  },
}

if validate_video_dep.found()
  gst_tools +=  {
    'gst-validate-images-check': {
      'files': files('gst-validate-images-check.c'),
      'deps': [validate_dep, validate_video_dep, gio_dep],
    },
  }
else
    message('Can not build gst-validate-images-check' + apiversion)
endif

rtsp_server_dep = dependency('gstreamer-rtsp-server-' + apiversion,
  fallback: ['gst-rtsp-server', 'gst_rtsp_server_dep'],
  version : gst_req,
  required: false)

if rtsp_server_dep.found()
    gst_tools += {'gst-validate-rtsp-server': {
        'files': files('gst-validate-rtsp-server.c'),
        'deps': [validate_dep, rtsp_server_dep, gio_dep],
      }
    }
else
    message('Can not build gst-validate-rtsp-server-' + apiversion)
endif

gst_transcoder_dep = dependency('gstreamer-transcoder-' + apiversion, version : gst_req,
    fallback : ['gst-plugins-bad', 'gst_transcoder_dep'], required: false)
if gst_transcoder_dep.found()
    gst_tools += {'gst-validate-transcoding': {
        'files': files('gst-validate-transcoding.c'),
        'deps': [validate_dep, gst_transcoder_dep, gio_dep],
      }
    }
else
  message('Can not build gst-validate-transcoding-' + apiversion)
endif

if not get_option('tools').disabled() and not static_build

  foreach tool, data: gst_tools
    if not data.has_key('config_data')
      exe_name = '@0@-@1@'.format(tool, apiversion)
      executable(
        exe_name,
        data.get('files'),
        install: true,
        install_tag: 'bin-devel',
        include_directories : inc_dirs,
        dependencies : data.get('deps'),
        c_args : [gst_c_args] + ['-DG_LOG_DOMAIN="@0@"'.format(exe_name)],
      )
    endif
  endforeach
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
