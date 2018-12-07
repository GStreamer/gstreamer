androidmedia_sources = [
  'gstahcsrc.c',
  'gstahssrc.c',
  'gstamcaudiodec.c',
  'gstamc.c',
  'gstamcsurface.c',
  'gstamcsurfacetexture.c',
  'gstamcvideodec.c',
  'gstamcvideoenc.c',
  'gst-android-graphics-imageformat.c',
  'gst-android-hardware-camera.c',
  'gst-android-hardware-sensor.c',
  'gstjniutils.c'
]

androidmedia_java_sources = [
  'org/freedesktop/gstreamer/androidmedia/GstAhcCallback.java',
  'org/freedesktop/gstreamer/androidmedia/GstAhsCallback.java',
  'org/freedesktop/gstreamer/androidmedia/GstAmcOnFrameAvailableListener.java',
]

if host_system != 'android' or get_option('androidmedia').disabled()
  subdir_done()
endif

if not gstgl_dep.found()
  if get_option('androidmedia').enabled()
    error('androidmedia plugin enabled but GL support was not detected')
  endif
  subdir_done()
endif

have_jni_h = cc.has_header('jni.h', required : false)
if not have_jni_h and get_option('androidmedia').enabled()
  error('androidmedia plugin enabled but jni.h not found')
endif

if have_jni_h
  gstandroidmedia = library('gstandroidmedia',
    androidmedia_sources,
    c_args : gst_plugins_bad_args,
    include_directories : [configinc],
    dependencies : [gstgl_dep, gstpbutils_dep, gstaudio_dep, gstvideo_dep, gstphotography_dep, gmodule_dep, orc_dep],
    install : true,
    install_dir : plugins_install_dir)
  pkgconfig.generate(gstandroidmedia, install_dir : plugins_pkgconfig_install_dir)

  install_data(androidmedia_java_sources,
    install_dir : join_paths(get_option('datadir'), 'gst-android/ndk-build/androidmedia')
  )
endif
