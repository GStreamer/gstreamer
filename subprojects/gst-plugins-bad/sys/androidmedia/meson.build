androidmedia_sources = [
  'gstaassetsrc.c',
  'gstamcaudiodec.c',
  'gstamc.c',
  'gstamc-codec.c',
  'gstamc-format.c',
  'gstamcaimagememory.c',
  'gstamcsurfacetexture.c',
  'gstamcvideodec.c',
  'gstamcvideoenc.c',
  'gstahcsrc.c',
  'gstahssrc.c',
  'gst-android-graphics-imageformat.c',
  'gst-android-hardware-camera.c',
  'gst-android-hardware-sensor.c',
  'gstjniutils.c',
  'jni/gstamc-jni.c',
  'jni/gstamc-codeclist-jni.c',
  'jni/gstamcsurface.c',
  'jni/gstamcsurfacetexture-jni.c',
]

ndk_sources = [
  'ndk/gstamc-codec-ndk.c',
  'ndk/gstamc-format-ndk.c',
]

androidmedia_headers = [
  'gstaassetsrc.h',
  'gstahcsrc.h',
  'gstahssrc.h',
  'gstamcaudiodec.h',
  'gstamc-codec.h',
  'gstamc-codeclist.h',
  'gstamc-constants.h',
  'gstamc-format.h',
  'gstamc.h',
  'gstamcaimagememory.h',
  'gstamcsurfacetexture.h',
  'gstamcvideodec.h',
  'gstamcvideoenc.h',
  'gst-android-graphics-imageformat.h',
  'gst-android-hardware-camera.h',
  'gst-android-hardware-sensor.h',
  'gstjniutils.h',
  'gstsensors.h',
]

androidmedia_java_sources = [
  'org/freedesktop/gstreamer/androidmedia/GstAhcCallback.java',
  'org/freedesktop/gstreamer/androidmedia/GstAhsCallback.java',
  'org/freedesktop/gstreamer/androidmedia/GstAmcOnFrameAvailableListener.java',
]

doc_sources = []
foreach s: androidmedia_sources + ndk_sources + androidmedia_headers
  doc_sources += meson.current_source_dir() / s
endforeach

plugin_sources += {
  'androidmedia': pathsep.join(doc_sources)
}

amc_opt = get_option('androidmedia')
amc_opt = amc_opt.require(gstgl_dep.found(),
  error_message: 'androidmedia plugin requires GL support',
)
amc_opt = amc_opt.require(host_system == 'android',
  error_message: 'androidmedia plugin requires android system',
)

have_jni_h = cc.has_header('jni.h', required : amc_opt)
have_asset_manager_jni_h = cc.has_header('android/asset_manager_jni.h',
  required : amc_opt)

if amc_opt.disabled() or not have_jni_h or not have_asset_manager_jni_h
  subdir_done()
endif

extra_cargs = [
  '-DHAVE_JNI_H',
  '-DPLUGIN_NAME=androidmedia',
  '-DPLUGIN_DESCRIPTION="Android Media plugin"'
]

# We don't actually link to libmediandk but use only type definitions and use
# dlopen/dlsym at runtime
amc_opt = amc_opt.require(cc.has_header('media/NdkMediaCodec.h'),
  error_message: 'androidmedia plugin requires NDK MediaCodec support',
)

if amc_opt.disabled()
  subdir_done()
endif

androidmedia_sources += ndk_sources
extra_deps = [cc.find_library('android')]

gstandroidmedia = library('gstandroidmedia',
  androidmedia_sources,
  c_args : [gst_plugins_bad_args, extra_cargs],
  include_directories : [configinc],
  dependencies : [gstgl_dep, gstpbutils_dep, gstaudio_dep, gstvideo_dep,
                  gstphotography_dep, gmodule_dep, orc_dep,
                  gstallocators_dep, extra_deps],
  install : true,
  install_dir : plugins_install_dir)

plugins += [gstandroidmedia]

install_data(androidmedia_java_sources,
  install_dir : join_paths(get_option('datadir'), 'gst-android/ndk-build/androidmedia')
)
