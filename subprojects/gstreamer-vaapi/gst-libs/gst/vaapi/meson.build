gstlibvaapi_sources = [
  'gstvaapiblend.c',
  'gstvaapibufferproxy.c',
  'gstvaapicodec_objects.c',
  'gstvaapicontext.c',
  'gstvaapidecoder.c',
  'gstvaapidecoder_dpb.c',
  'gstvaapidecoder_h264.c',
  'gstvaapidecoder_h265.c',
  'gstvaapidecoder_jpeg.c',
  'gstvaapidecoder_mpeg2.c',
  'gstvaapidecoder_mpeg4.c',
  'gstvaapidecoder_objects.c',
  'gstvaapidecoder_unit.c',
  'gstvaapidecoder_vc1.c',
  'gstvaapidecoder_vp8.c',
  'gstvaapidecoder_vp9.c',
  'gstvaapidisplay.c',
  'gstvaapifilter.c',
  'gstvaapiimage.c',
  'gstvaapiimagepool.c',
  'gstvaapiminiobject.c',
  'gstvaapiparser_frame.c',
  'gstvaapiprofile.c',
  'gstvaapiprofilecaps.c',
  'gstvaapisubpicture.c',
  'gstvaapisurface.c',
  'gstvaapisurface_drm.c',
  'gstvaapisurfacepool.c',
  'gstvaapisurfaceproxy.c',
  'gstvaapitexture.c',
  'gstvaapitexturemap.c',
  'gstvaapiutils.c',
  'gstvaapiutils_core.c',
  'gstvaapiutils_h264.c',
  'gstvaapiutils_h265.c',
  'gstvaapiutils_h26x.c',
  'gstvaapiutils_mpeg2.c',
  'gstvaapiutils_vpx.c',
  'gstvaapivalue.c',
  'gstvaapivideopool.c',
  'gstvaapiwindow.c',
  'video-format.c',
]

gstlibvaapi_headers = [
  'gstvaapiblend.h',
  'gstvaapibufferproxy.h',
  'gstvaapidecoder.h',
  'gstvaapidecoder_h264.h',
  'gstvaapidecoder_h265.h',
  'gstvaapidecoder_jpeg.h',
  'gstvaapidecoder_mpeg2.h',
  'gstvaapidecoder_mpeg4.h',
  'gstvaapidecoder_vc1.h',
  'gstvaapidecoder_vp8.h',
  'gstvaapidecoder_vp9.h',
  'gstvaapidisplay.h',
  'gstvaapifilter.h',
  'gstvaapiimage.h',
  'gstvaapiimagepool.h',
  'gstvaapiprofile.h',
  'gstvaapiprofilecaps.h',
  'gstvaapisubpicture.h',
  'gstvaapisurface.h',
  'gstvaapisurface_drm.h',
  'gstvaapisurfacepool.h',
  'gstvaapisurfaceproxy.h',
  'gstvaapitexture.h',
  'gstvaapitexturemap.h',
  'gstvaapitypes.h',
  'gstvaapiutils_h264.h',
  'gstvaapiutils_h265.h',
  'gstvaapiutils_mpeg2.h',
  'gstvaapiutils_vpx.h',
  'gstvaapivalue.h',
  'gstvaapivideopool.h',
  'gstvaapiwindow.h',
  'video-format.h',
]

if USE_ENCODERS
  gstlibvaapi_sources += [
      'gstvaapicodedbuffer.c',
      'gstvaapicodedbufferpool.c',
      'gstvaapicodedbufferproxy.c',
      'gstvaapiencoder.c',
      'gstvaapiencoder_h264.c',
      'gstvaapiencoder_h265.c',
      'gstvaapiencoder_jpeg.c',
      'gstvaapiencoder_mpeg2.c',
      'gstvaapiencoder_objects.c',
      'gstvaapiencoder_vp8.c',
    ]
  gstlibvaapi_headers += [
      'gstvaapicodedbuffer.h',
      'gstvaapicodedbufferpool.h',
      'gstvaapicodedbufferproxy.h',
      'gstvaapiencoder.h',
      'gstvaapiencoder_h264.h',
      'gstvaapiencoder_h265.h',
      'gstvaapiencoder_jpeg.h',
      'gstvaapiencoder_mpeg2.h',
      'gstvaapiencoder_vp8.h',
    ]
endif

if USE_VP9_ENCODER
  gstlibvaapi_sources += 'gstvaapiencoder_vp9.c'
  gstlibvaapi_headers += 'gstvaapiencoder_vp9.h'
endif

if USE_AV1_DECODER
  gstlibvaapi_sources += 'gstvaapidecoder_av1.c'
  gstlibvaapi_headers += 'gstvaapidecoder_av1.h'
endif

if USE_DRM
  gstlibvaapi_sources += [
      'gstvaapidisplay_drm.c',
      'gstvaapiwindow_drm.c',
    ]
  gstlibvaapi_headers += [
      'gstvaapidisplay_drm.h',
      'gstvaapiwindow_drm.h',
    ]
endif

if USE_X11
  gstlibvaapi_sources += [
      'gstvaapidisplay_x11.c',
      'gstvaapiutils_x11.c',
      'gstvaapiwindow_x11.c',
    ]
  gstlibvaapi_headers += [
      'gstvaapidisplay_x11.h',
      'gstvaapiwindow_x11.h',
    ]
endif

if USE_GLX
  gstlibvaapi_sources += [
      'gstvaapidisplay_glx.c',
      'gstvaapitexture_glx.c',
      'gstvaapiutils_glx.c',
      'gstvaapiwindow_glx.c',
    ]
  gstlibvaapi_headers += [
      'gstvaapidisplay_glx.h',
      'gstvaapitexture.h',
      'gstvaapitexture_glx.h',
      'gstvaapiwindow_glx.h',
    ]
endif

if USE_EGL
  gstlibvaapi_sources += [
      'gstvaapidisplay_egl.c',
      'gstvaapisurface_egl.c',
      'gstvaapitexture_egl.c',
      'gstvaapiutils_egl.c',
      'gstvaapiwindow_egl.c',
    ]
  gstlibvaapi_headers += [
      'gstvaapidisplay_egl.h',
      'gstvaapisurface_egl.h',
      'gstvaapitexture_egl.h',
      'gstvaapiwindow_egl.h',
    ]
endif

if USE_WAYLAND
  # The XDG shell interface needs to be generated first
  wayland_protocols_basedir = wayland_protocols_dep.get_variable('pkgdatadir')
  xdg_shell_xml_spec = join_paths(wayland_protocols_basedir, 'stable', 'xdg-shell', 'xdg-shell.xml')
  xdg_shell_header = custom_target('vaapi-xdg-shell-client-header',
      command: [ wayland_scanner_bin, 'client-header', '@INPUT@', '@OUTPUT@' ],
      input: xdg_shell_xml_spec,
      output: 'xdg-shell-client-protocol.h')
  xdg_shell_code = custom_target('vaapi-xdg-shell-client-code',
      command: [ wayland_scanner_bin, 'private-code', '@INPUT@', '@OUTPUT@' ],
      input: xdg_shell_xml_spec,
      output: 'xdg-shell-client-protocol.c')
  dmabuf_xml_spec = join_paths(wayland_protocols_basedir, 'unstable', 'linux-dmabuf', 'linux-dmabuf-unstable-v1.xml')
  dmabuf_header = custom_target('vaapi-dmabuf-client-header',
      command: [ wayland_scanner_bin, 'client-header', '@INPUT@', '@OUTPUT@' ],
      input: dmabuf_xml_spec,
      output: 'linux-dmabuf-unstable-v1-client-protocol.h')
  dmabuf_code = custom_target('vaapi-dmabuf-client-code',
      command: [ wayland_scanner_bin, 'private-code', '@INPUT@', '@OUTPUT@' ],
      input: dmabuf_xml_spec,
      output: 'linux-dmabuf-unstable-v1-client-protocol.c')

  gstlibvaapi_sources += [
      'gstvaapidisplay_wayland.c',
      'gstvaapiwindow_wayland.c',
      xdg_shell_header,
      xdg_shell_code,
      dmabuf_header,
      dmabuf_code,
  ]
  gstlibvaapi_headers += [
      'gstvaapidisplay_wayland.h',
      'gstvaapiwindow_wayland.h',
    ]
endif

gstlibvaapi_deps = [ gstbase_dep,
                     gstvideo_dep,
                     gstgl_dep,
                     gstglproto_dep,
                     gstcodecparsers_dep,
                     libva_dep,
                     libm ]
if USE_DRM
  gstlibvaapi_deps  += [libva_drm_dep, libdrm_dep, libudev_dep]
endif
if USE_EGL
  gstlibvaapi_deps  += [egl_dep, gmodule_dep, gstglegl_dep]
endif
if USE_GLX
  gstlibvaapi_deps  += [libva_x11_dep, x11_dep, gl_dep, libdl_dep]
endif
if USE_WAYLAND
  gstlibvaapi_deps  += [libva_wayland_dep, gstglwayland_dep, wayland_client_dep, wayland_protocols_dep]
endif
if USE_X11
  gstlibvaapi_deps  += [libva_x11_dep, x11_dep, xrandr_dep, gstglx11_dep]
endif

gstlibvaapi = static_library('gstlibvaapi-@0@'.format(api_version),
  gstlibvaapi_sources + gstlibvaapi_headers,
  c_args : gstreamer_vaapi_args + [ '-DGST_USE_UNSTABLE_API', '-DGST_VAAPI_VERSION_ID="@0@"'.format(gst_version)],
  include_directories: [configinc, libsinc],
  dependencies : gstlibvaapi_deps,
)

gstlibvaapi_dep = declare_dependency(link_with: gstlibvaapi,
  include_directories : [libsinc],
  dependencies : gstlibvaapi_deps)
