applemedia_sources = [
    'plugin.m',
    'corevideomemory.c',
    'corevideobuffer.c',
    'coremediabuffer.c',
    'videotexturecache.m',
    'videotexturecache-gl.m',
    'glcontexthelper.c',
    'vtutil.c',
    'helpers.c',
]

applemedia_args = [
    '-DGST_USE_UNSTABLE_API'
]

applemedia_objc_args = []
applemedia_option = get_option('applemedia')
if not ['darwin', 'ios'].contains(host_system) or applemedia_option.disabled()
  applemedia_found_deps = false
  subdir_done()
endif

if ['darwin', 'ios'].contains(host_system)
  objc = meson.get_compiler('objc')
  if not objc.has_argument('-fobjc-arc')
    error('ARC is required for building')
  endif

  applemedia_objc_args += ['-fobjc-arc']

  objcpp = meson.get_compiler('objcpp')
endif

applemedia_deps = []
corefoundation_dep = dependency('CoreFoundation', required : applemedia_option)
coremedia_dep = dependency('CoreMedia', required : applemedia_option)
corevideo_dep = dependency('CoreVideo', required : applemedia_option)
applemedia_deps += [corefoundation_dep, coremedia_dep, corevideo_dep]

avfoundation_dep = dependency('AVFoundation', required : applemedia_option)
if avfoundation_dep.found()
    cdata.set('HAVE_AVFOUNDATION', 1)
    applemedia_sources += [
        'avfvideosrc.m',
        'avfassetsrc.m',
        'avsamplevideosink.m',
        'avfdeviceprovider.m',
    ]
    applemedia_deps += [avfoundation_dep]
endif

videotoolbox_dep = dependency('VideoToolbox', required : applemedia_option)
if videotoolbox_dep.found()
    cdata.set('HAVE_VIDEOTOOLBOX', 1)
    applemedia_sources += [
        'vtenc.c',
        'vtdec.c'
    ]
    applemedia_deps += [videotoolbox_dep]
endif

if host_system == 'ios'
    applemedia_sources += [
        'iosassetsrc.m',
        'iosglmemory.c'
    ]
    applemedia_objc_args += ['-fobjc-abi-version=2', '-fobjc-legacy-dispatch']

    ios_media_dep = dependency('appleframeworks', modules : ['Foundation', 'AssetsLibrary'], required : applemedia_option)
    iosurface_dep = dependency('IOSurface', required : applemedia_option)
    applemedia_deps += [ios_media_dep, iosurface_dep]
else
    applemedia_sources += [
        'iosurfaceglmemory.c'
    ]
    cocoa_dep = dependency('Cocoa', required : applemedia_option)
    iosurface_dep = dependency('IOSurface', required : applemedia_option)
    applemedia_opengl_dep = dependency('appleframeworks', modules : ['OpenGL'], required : applemedia_option)
    applemedia_deps += [cocoa_dep, applemedia_opengl_dep, iosurface_dep]
endif

applemedia_found_deps = true
foreach framework : applemedia_deps
    if not framework.found()
        applemedia_found_deps = false
    endif
endforeach

applemedia_objcpp_args = []
if gstvulkan_dep.found() and have_objcpp
  moltenvk_dep = cc.find_library('MoltenVK', required : false)
  metal_dep = dependency('appleframeworks', modules : ['Metal'], required : false)
  if metal_dep.found() and moltenvk_dep.found() and cc.has_header ('MoltenVK/vk_mvk_moltenvk.h')
    applemedia_deps += [moltenvk_dep, gstvulkan_dep, metal_dep]
    applemedia_sources += [
      'videotexturecache-vulkan.mm',
      'iosurfacevulkanmemory.c',
    ]
    applemedia_args += ['-DAPPLEMEDIA_MOLTENVK']
    # override_options : ['cpp_std=c++11'] doesn't seem to work for objcpp
    applemedia_objcpp_args += objcpp.get_supported_arguments([
      '-std=c++11',
    ])
  endif
endif

gstapplemedia_libs = []
gstapplemedia_whole_libs = []
gstapplemedia_link_args = []
gstapplemedia_swiftinc = []
applemedia_swift_option = get_option('applemedia-swift')
have_swift = add_languages('swift', required: applemedia_swift_option, native: false)
applemedia_swift_option = applemedia_swift_option.require(meson.version().version_compare('>=1.5'),
    error_message: 'Need Meson >=1.5')
applemedia_swift_option = applemedia_swift_option.require(host_machine.subsystem() == 'macos',
    error_message: 'Only macOS is supported at present')
applemedia_swift_option = applemedia_swift_option.require(have_swift,
    error_message: 'Need the Swift compiler')
if have_swift
  swiftc = meson.get_compiler('swift', native: false)
  swiftpm = find_program('swift', required : applemedia_swift_option)
  applemedia_swift_option = applemedia_swift_option.require(swiftc.version().version_compare('>=6.0'),
      error_message: 'Swift compiler < 6.0 segfaults due to bugs')
  applemedia_swift_option = applemedia_swift_option.require(swiftpm.found(),
      error_message: 'Need the Swift package manager')
endif
if applemedia_swift_option.allowed()
  # Swift-based elements are hidden behind an option because they require an RPATH (/usr/lib/swift)
  # to be added to the resulting .dylib. We're not sure yet if this won't, for example, prevent
  # apps from being submitted to the App Store, so we need to be able to disable it just in case.
  # Currently we only have the SCKit element in Swift, this guard will have to change if more is added
  screencapturekit_dep = dependency('ScreenCaptureKit', required : applemedia_swift_option)
  if screencapturekit_dep.found()
    subdir('swift')
  endif
endif

if applemedia_found_deps
    gstapplemedia = shared_library('gstapplemedia',
        applemedia_sources,
        c_args : gst_plugins_bad_args + applemedia_args,
        objc_args : gst_plugins_bad_args + applemedia_args + applemedia_objc_args,
        objcpp_args : gst_plugins_bad_args + applemedia_args + applemedia_objc_args + applemedia_objcpp_args,
        link_with: gstapplemedia_libs,
        link_whole: gstapplemedia_whole_libs,
        link_args : noseh_link_args + gstapplemedia_link_args,
        include_directories : [configinc, libsinc] + gstapplemedia_swiftinc,
        dependencies : [gstvideo_dep, gstpbutils_dep, gst_dep, gstbase_dep, gstgl_dep, gstglproto_dep, gstcodecparsers_dep] + applemedia_deps,
        override_options : ['cpp_std=c++11'],
        install : true,
        install_dir : plugins_install_dir,
    )
    plugins += [gstapplemedia]
endif
