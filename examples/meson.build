examples = [
  'test-appsrc',
  'test-appsrc2',
  'test-auth',
  'test-auth-digest',
  'test-launch',
  'test-mp4',
  'test-multicast2',
  'test-multicast',
  'test-netclock',
  'test-netclock-client',
  'test-ogg',
  'test-onvif-client',
  'test-onvif-server',
  'test-readme',
  'test-record-auth',
  'test-record',
  'test-sdp',
  'test-uri',
  'test-video',
  'test-video-rtx',
]

foreach example : examples
  executable(example, '@0@.c'.format(example),
    c_args : rtspserver_args,
    include_directories : rtspserver_incs,
    dependencies : [glib_dep, gst_dep, gstapp_dep, gstnet_dep, gst_rtsp_server_dep],
    install: false)
endforeach

cgroup_dep = dependency('libcgroup', version : '>= 0.26', required : false)
if cgroup_dep.found()
  executable('test-cgroups', 'test-cgroups.c',
    c_args : rtspserver_args,
    include_directories : rtspserver_incs,
    dependencies : [glib_dep, gst_dep, gstnet_dep, gst_rtsp_server_dep, cgroup_dep],
    install: false)
endif
