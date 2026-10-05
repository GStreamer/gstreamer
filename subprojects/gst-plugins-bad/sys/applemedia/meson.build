applemedia_sources = [
  'avfassetsrc.m',
  'avsamplevideosink.m',
  'coremediabuffer.c',
  'corevideobuffer.c',
  'corevideomemory.c',
  'helpers.m',
  'plugin.m',
  'videotexturecache.m',
  'vtenc.c',
  'vtdec.c',
  'vtutil.c',
]

applemedia_headers = [
  'avfassetsrc.h',
  'avfdeviceprovider.h',
  'avfvideosrc.h',
  'avsamplevideosink.h',
  'coremediabuffer.h',
  'corevideobuffer.h',
  'corevideomemory.h',
  'helpers.h',
  'iosassetsrc.h',
  'videotexturecache.h',
  'vtdec.h',
  'vtenc.h',
  'vtutil.h',
]

applemedia_args = [
  '-Dgst_core_media_buffer_new=gst_core_media_buffer_priv_new',
  '-Dgst_core_media_buffer_get_type=gst_core_media_buffer_priv_get_type',
  '-Dgst_core_media_buffer_get_pixel_buffer=gst_core_media_buffer_priv_get_pixel_buffer',
  '-Dgst_core_video_buffer_new=gst_core_video_buffer_priv_new',
  '-Dgst_core_video_buffer_get_type=gst_core_video_buffer_priv_get_type',
  '-DGST_USE_UNSTABLE_API'
]

avcapturedevice_sources = [
  'avfvideosrc.m',
  'avfdeviceprovider.m',
]

ios_deprecated_sources = [
  'iosassetsrc.m',
]

gl_sources = [
  'glcontexthelper.c',
  'videotexturecache-gl.m',
]

gl_headers = [
  'glcontexthelper.h',
  'iosglmemory.h',
  'iosurfaceglmemory.h',
  'videotexturecache-gl.h',
]

ios_gl_sources = [
  'iosglmemory.c',
]

macos_gl_sources = [
  'iosurfaceglmemory.c'
]

vulkan_sources = [
  'videotexturecache-vulkan.mm',
  'iosurfacevulkanmemory.c',
]

vulkan_headers = [
  'iosurfacevulkanmemory.h',
  'metal-helpers.h',
  'videotexturecache-vulkan.h',
]

doc_sources = []
foreach s: applemedia_sources + avcapturedevice_sources + gl_sources + ios_gl_sources + ios_deprecated_sources + macos_gl_sources + vulkan_sources + applemedia_headers + gl_headers + vulkan_headers
  doc_sources += meson.current_source_dir() / s
endforeach

plugin_sources += {
  'applemedia': pathsep.join(doc_sources)
}

applemedia_objc_args = []
applemedia_deps = [
  gstvideo_dep,
  gstpbutils_dep,
  gst_dep,
  gstbase_dep,
  gstcodecparsers_dep,
  gstallocators_dep,
]
applemedia_found_deps = false
applemedia_option = get_option('applemedia')
if host_system != 'darwin' or applemedia_option.disabled() or not have_objcpp
  subdir_done()
endif

if cc.get_define('MAC_OS_X_VERSION_MAX_ALLOWED', prefix: '#include <AvailabilityMacros.h>').to_int() < 110000
  if applemedia_option.enabled()
    error('applemedia plugin needs Xcode 12.4 or newer')
  endif
  warning('Disabling applemedia plugin: needs Xcode 12.4 or newer')
  subdir_done()
endif

objc = meson.get_compiler('objc')
if not objc.has_argument('-fobjc-arc')
  error('ARC is required for building')
endif

applemedia_objc_args += ['-fobjc-arc']
objcpp = meson.get_compiler('objcpp')

applemedia_frameworks = [
  dependency('AVFoundation', required: applemedia_option),
  dependency('CoreFoundation', required : applemedia_option),
  dependency('CoreMedia', required : applemedia_option),
  dependency('CoreVideo', required : applemedia_option),
]

if subsystem == 'macos' or subsystem.startswith('ios')
  applemedia_sources += avcapturedevice_sources
endif

if not subsystem.startswith('watchos')
  applemedia_frameworks += [dependency('VideoToolbox', required : applemedia_option)]
endif

if subsystem == 'macos'
  cocoa_dep = dependency('Cocoa', required : applemedia_option)
  iosurface_dep = dependency('IOSurface', required : applemedia_option)
  applemedia_frameworks += [cocoa_dep, iosurface_dep]
else
  if subsystem.startswith('ios')
    applemedia_sources += ios_deprecated_sources
    ios_media_dep = dependency('appleframeworks', modules : ['Foundation', 'AssetsLibrary'], required : applemedia_option)
  else
    ios_media_dep = dependency('appleframeworks', modules : ['Foundation'], required : applemedia_option)
  endif
  iosurface_dep = dependency('IOSurface', required : applemedia_option)
  applemedia_frameworks += [ios_media_dep, iosurface_dep]
  applemedia_objc_args += ['-fobjc-abi-version=2', '-fobjc-legacy-dispatch']
endif

if gstgl_dep.found()
  applemedia_sources += gl_sources
  applemedia_deps += [gstgl_dep, gstglproto_dep]
  applemedia_args += ['-DHAVE_GST_GL']

  if subsystem == 'macos'
    applemedia_sources += macos_gl_sources
    applemedia_frameworks += [dependency('appleframeworks', modules : ['OpenGL'], required : applemedia_option)]
  elif subsystem.startswith('ios') or subsystem.startswith('tvos')
    applemedia_sources += ios_gl_sources
  endif
endif

applemedia_found_deps = true
foreach framework : applemedia_frameworks
  if not framework.found()
    applemedia_found_deps = false
  endif
endforeach

applemedia_objcpp_args = []
applemedia_kwargs = {}
vulkan_option = get_option('vulkan')
vk_required = applemedia_option.enabled() and vulkan_option.enabled()
if gstvulkan_dep.found() and not vulkan_option.disabled()
  moltenvk_dep = objcpp.find_library('MoltenVK', required: false)
  if moltenvk_dep.found()
    have_mvk_header = objcpp.has_header('MoltenVK/vk_mvk_moltenvk.h', required: vk_required)
  else
    moltenvk_dep = dependency('MoltenVK', fallback: 'moltenvk', required: vk_required)
    have_mvk_header = true
  endif
  metal_dep = dependency('Metal', required: vk_required)
  if metal_dep.found() and moltenvk_dep.found() and have_mvk_header
    applemedia_frameworks += [moltenvk_dep, gstvulkan_dep, metal_dep]
    applemedia_sources += vulkan_sources
    applemedia_args += ['-DAPPLEMEDIA_MOLTENVK']
    if moltenvk_dep.type_name() == 'internal'
      # Needed to find libMoltenVK.dylib when installed
      applemedia_kwargs += {'install_rpath': '@loader_path/..'}
    endif
    # override_options : ['cpp_std=c++11'] doesn't seem to work for objcpp
    applemedia_objcpp_args += objcpp.get_supported_arguments([
      '-std=c++11',
    ])
  endif
endif

if applemedia_found_deps
  gstapplemedia = library('gstapplemedia',
    applemedia_sources,
    c_args : gst_plugins_bad_args + applemedia_args,
    objc_args : gst_plugins_bad_args + applemedia_args + applemedia_objc_args,
    objcpp_args : gst_plugins_bad_args + applemedia_args + applemedia_objc_args + applemedia_objcpp_args,
    link_args : noseh_link_args,
    kwargs: applemedia_kwargs,
    include_directories : [configinc, libsinc],
    dependencies : applemedia_deps + applemedia_frameworks,
    override_options : ['cpp_std=c++11'],
    install : true,
    install_dir : plugins_install_dir,
  )
  plugins += [gstapplemedia]
endif
