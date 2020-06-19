examples = [
  'test-vaapisink',
  'test-vaapipostproc',
  'test-roi',
]

foreach example : examples
  executable(example, '@0@.c'.format(example),
    c_args : gstreamer_vaapi_args,
    include_directories: [configinc, libsinc],
    dependencies : [gst_dep, gstvideo_dep],
    install: false)
endforeach

if USE_X11 and USE_WAYLAND
if gtk_dep.found()
  executable('test-vaapicontext', 'test-vaapicontext.c',
    c_args : gstreamer_vaapi_args,
    include_directories: [configinc, libsinc],
    dependencies : [ gst_dep,
                     gstvideo_dep,
                     libva_dep,
                     x11_dep,
                     gtk_dep,
                     libva_wayland_dep,
                     libva_x11_dep ],
    install: false)
endif
endif
