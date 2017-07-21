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

applemedia_deps_found = false
applemedia_frameworks = []
corefoundation_dep = dependency('CoreFoundation', required : false)
coremedia_dep = dependency('CoreMedia', required : false)
corevideo_dep = dependency('CoreVideo', required : false)
audiotoolbox_dep = dependency('AudioToolbox', required : false)
applemedia_frameworks += [corefoundation_dep, coremedia_dep, corevideo_dep, audiotoolbox_dep]

avfoundation_dep = dependency('AVFoundation', required : false)
if avfoundation_dep.found()
    applemedia_sources += [
        'avfvideosrc.m',
        'avfassetsrc.m',
        'avsamplevideosink.m'
    ]
    applemedia_frameworks += [avfoundation_dep]
endif

videotoolbox_dep = dependency('VideoToolbox', required : false)
if videotoolbox_dep.found()
    applemedia_sources += [
        'vtenc.c',
        'vtdec.c'
    ]
    applemedia_frameworks += [videotoolbox_dep]
endif

if cdata.has('HAVE_IOS')
    applemedia_sources += [
        'iosassetsrc.m',
        'iosglmemory.c'
    ]
    applemedia_objc_args += ['-fobjc-abi-version=2', '-fobjc-legacy-dispatch']

    foundation_dep = dependency('Foundation', required : false)
    coremedia_dep = dependency('AssetsLibrary', required : false)
    applemedia_frameworks += [foundation_dep, coremedia_dep]
else
    applemedia_sources += [
        'iosurfacememory.c'
    ]
    applemedia_objc_args += ['-mmacosx-version-min=10.8']
    cocoa_dep = dependency('Cocoa', required : false)
    iosurface_dep = dependency('IOSurface', required : false)
    applemedia_opengl_dep = dependency('OpenGL', required : false)
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
endif