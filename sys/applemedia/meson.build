applemedia_sources = [
    'plugin.m',
    'vtutil.c',
    'corevideomemory.c',
    'corevideobuffer.c',
    'coremediabuffer.c',
    'videotexturecache.m',
    'atdec.c',
    'glcontexthelper.c'
]

applemedia_args = [
    '-Dgst_core_media_buffer_new=gst_core_media_buffer_priv_new',
    '-Dgst_core_media_buffer_get_type=gst_core_media_buffer_priv_get_type',
    '-Dgst_core_media_buffer_get_pixel_buffer=gst_core_media_buffer_priv_get_pixel_buffer',
    '-Dgst_core_video_buffer_new=gst_core_video_buffer_priv_new',
    '-Dgst_core_video_buffer_get_type=gst_core_video_buffer_priv_get_type'
]

applemedia_objc_args = []
applemedia_option = get_option('applemedia')
if not ['darwin', 'ios'].contains(host_system) or applemedia_option.disabled()
  subdir_done()
endif

applemedia_frameworks = []
corefoundation_dep = dependency('CoreFoundation', required : applemedia_option)
coremedia_dep = dependency('CoreMedia', required : applemedia_option)
corevideo_dep = dependency('CoreVideo', required : applemedia_option)
audiotoolbox_dep = dependency('AudioToolbox', required : applemedia_option)
applemedia_frameworks += [corefoundation_dep, coremedia_dep, corevideo_dep, audiotoolbox_dep]

avfoundation_dep = dependency('AVFoundation', required : applemedia_option)
if avfoundation_dep.found()
    applemedia_sources += [
        'avfvideosrc.m',
        'avfassetsrc.m',
        'avsamplevideosink.m',
        'avfdeviceprovider.m',
    ]
    applemedia_frameworks += [avfoundation_dep]
endif

videotoolbox_dep = dependency('VideoToolbox', required : applemedia_option)
if videotoolbox_dep.found()
    applemedia_sources += [
        'vtenc.c',
        'vtdec.c'
    ]
    applemedia_frameworks += [videotoolbox_dep]
endif

if host_system == 'ios'
    applemedia_sources += [
        'iosassetsrc.m',
        'iosglmemory.c'
    ]
    applemedia_objc_args += ['-fobjc-abi-version=2', '-fobjc-legacy-dispatch']

    ios_media_dep = dependency('appleframeworks', modules : ['Foundation', 'AssetsLibrary'], required : applemedia_option)
    applemedia_frameworks += [ios_media_dep]
else
    applemedia_sources += [
        'iosurfacememory.c'
    ]
    applemedia_objc_args += ['-mmacosx-version-min=10.8']
    cocoa_dep = dependency('Cocoa', required : applemedia_option)
    iosurface_dep = dependency('IOSurface', required : applemedia_option)
    applemedia_opengl_dep = dependency('appleframeworks', modules : ['OpenGL'], required : applemedia_option)
    applemedia_frameworks += [cocoa_dep, applemedia_opengl_dep, iosurface_dep]
endif

applemedia_found_deps = true
foreach framework : applemedia_frameworks
    if not framework.found()
        applemedia_found_deps = false
    endif
endforeach

if applemedia_found_deps
    gstapplemedia = library('gstapplemedia',
        applemedia_sources,
        c_args : gst_plugins_bad_args + applemedia_args,
        objc_args : gst_plugins_bad_args + applemedia_args + applemedia_objc_args,
        link_args : noseh_link_args,
        include_directories : [configinc, libsinc],
        dependencies : [gstvideo_dep, gstaudio_dep, gstpbutils_dep, gst_dep, gstbase_dep, gstgl_dep] + applemedia_frameworks,
        install : true,
        install_dir : plugins_install_dir,
    )
    pkgconfig.generate(gstapplemedia, install_dir : plugins_pkgconfig_install_dir)
endif
