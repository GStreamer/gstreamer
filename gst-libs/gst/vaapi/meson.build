gstlibvaapi_sources = [
  'gstvaapibufferproxy.c',
  'gstvaapicodec_objects.c',
  'gstvaapicontext.c',
  'gstvaapicontext_overlay.c',
  'gstvaapidecoder.c',
  'gstvaapidecoder_dpb.c',
  'gstvaapidecoder_h264.c',
  'gstvaapidecoder_mpeg2.c',
  'gstvaapidecoder_mpeg4.c',
  'gstvaapidecoder_objects.c',
  'gstvaapidecoder_unit.c',
  'gstvaapidecoder_vc1.c',
  'gstvaapidisplay.c',
  'gstvaapidisplaycache.c',
  'gstvaapifilter.c',
  'gstvaapiimage.c',
  'gstvaapiimagepool.c',
  'gstvaapiminiobject.c',
  'gstvaapiobject.c',
  'gstvaapiparser_frame.c',
  'gstvaapipixmap.c',
  'gstvaapiprofile.c',
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
  'gstvaapivalue.c',
  'gstvaapivideopool.c',
  'gstvaapiwindow.c',
  'video-format.c',
]

gstlibvaapi_headers = [
  'gstvaapibufferproxy.h',
  'gstvaapidecoder.h',
  'gstvaapidecoder_h264.h',
  'gstvaapidecoder_h265.h',
  'gstvaapidecoder_mpeg2.h',
  'gstvaapidecoder_mpeg4.h',
  'gstvaapidecoder_vc1.h',
  'gstvaapidisplay.h',
  'gstvaapifilter.h',
  'gstvaapiimage.h',
  'gstvaapiimagepool.h',
  'gstvaapiobject.h',
  'gstvaapipixmap.h',
  'gstvaapiprofile.h',
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
  'gstvaapivalue.h',
  'gstvaapivideopool.h',
  'gstvaapiwindow.h',
  'video-format.h',
]

if USE_JPEG_DECODER
  gstlibvaapi_sources += 'gstvaapidecoder_jpeg.c'
  gstlibvaapi_headers += 'gstvaapidecoder_jpeg.h'
endif

if USE_VP8_DECODER
  gstlibvaapi_sources += 'gstvaapidecoder_vp8.c'
  gstlibvaapi_headers += 'gstvaapidecoder_vp8.h'
endif

if USE_H265_DECODER
  gstlibvaapi_sources += 'gstvaapidecoder_h265.c'
  gstlibvaapi_headers += 'gstvaapidecoder_h265.h'
endif

if USE_VP9_DECODER
  gstlibvaapi_sources += 'gstvaapidecoder_vp9.c'
  gstlibvaapi_headers += 'gstvaapidecoder_vp9.h'
endif

if USE_ENCODERS
  gstlibvaapi_sources += [
      'gstvaapicodedbuffer.c',
      'gstvaapicodedbufferpool.c',
      'gstvaapicodedbufferproxy.c',
      'gstvaapiencoder.c',
      'gstvaapiencoder_h264.c',
      'gstvaapiencoder_mpeg2.c',
      'gstvaapiencoder_objects.c',
    ]
  gstlibvaapi_headers += [
      'gstvaapicodedbuffer.h',
      'gstvaapicodedbufferpool.h',
      'gstvaapicodedbufferproxy.h',
      'gstvaapiencoder.h',
      'gstvaapiencoder_h264.h',
      'gstvaapiencoder_mpeg2.h',
    ]
endif

if USE_JPEG_ENCODER
  gstlibvaapi_sources += 'gstvaapiencoder_jpeg.c'
  gstlibvaapi_headers += 'gstvaapiencoder_jpeg.h'
endif

if USE_VP8_ENCODER
  gstlibvaapi_sources += 'gstvaapiencoder_vp8.c'
  gstlibvaapi_headers += 'gstvaapiencoder_vp8.h'
endif

if USE_H265_ENCODER
  gstlibvaapi_sources += 'gstvaapiencoder_h265.c'
  gstlibvaapi_headers += 'gstvaapiencoder_h265.h'
endif

if USE_VP9_ENCODER
  gstlibvaapi_sources += 'gstvaapiencoder_vp9.c'
  gstlibvaapi_headers += 'gstvaapiencoder_vp9.h'
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
      'gstvaapipixmap_x11.c',
      'gstvaapiutils_x11.c',
      'gstvaapiwindow_x11.c',
    ]
  gstlibvaapi_headers += [
      'gstvaapidisplay_x11.h',
      'gstvaapipixmap_x11.h',
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
  gstlibvaapi_sources += [
      'gstvaapidisplay_wayland.c',
      'gstvaapiwindow_wayland.c',
    ]
  gstlibvaapi_headers += [
      'gstvaapidisplay_wayland.h',
      'gstvaapiwindow_wayland.h',
    ]
endif

gstlibvaapi_deps = [gstbase_dep, gstvideo_dep, gstcodecparsers_dep, libva_dep, gstvaapi_baseutils_dep]
if USE_DRM
  gstlibvaapi_deps  += [libva_drm_dep, libdrm_dep, libudev_dep]
endif
if USE_EGL
  gstlibvaapi_deps  += [egl_dep, gmodule_dep]
endif
if USE_GLX
  gstlibvaapi_deps  += [libva_x11_dep, x11_dep, gl_dep, libdl_dep]
endif
if USE_WAYLAND
  gstlibvaapi_deps  += [libva_wayland_dep, wayland_client_dep]
endif
if USE_X11
  gstlibvaapi_deps  += [libva_x11_dep, x11_dep, xrandr_dep, xrender_dep]
endif

gstlibvaapi = static_library('gstlibvaapi-@0@'.format(api_version),
  gstlibvaapi_sources,
  c_args : gstreamer_vaapi_args + ['-DIN_LIBGSTVAAPI', '-DIN_LIBGSTVAAPI_CORE', '-DGST_USE_UNSTABLE_API', '-DGST_VAAPI_VERSION_ID="@0@"'.format(gst_version)],
  include_directories: [configinc, libsinc],
  version : libversion,
  soversion : soversion,
  dependencies : gstlibvaapi_deps,
)

gstlibvaapi_dep = declare_dependency(link_with: gstlibvaapi,
  include_directories : [libsinc],
  dependencies : gstlibvaapi_deps)
