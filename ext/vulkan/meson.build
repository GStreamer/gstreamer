vulkan_sources = [
  'gstvulkan.c',
  'vkbuffermemory.c',
  'vkbufferpool.c',
  'vkdevice.c',
  'vkdisplay.c',
  'vkerror.c',
  'vkfence.c',
  'vkimagememory.c',
  'vkinstance.c',
  'vkmemory.c',
  'vkqueue.c',
  'vksink.c',
  'vkswapper.c',
  'vktrash.c',
  'vkupload.c',
  'vkutils.c',
  'vkwindow.c',
]

vulkan_objc_args = []
vulkan_defines = []
optional_deps = []
if get_option('vulkan').disabled()
  subdir_done()
endif

if host_system == 'ios'
  vulkan_dep = cc.find_library('MoltenVK', required : get_option('vulkan'))
else
  vulkan_dep = cc.find_library('vulkan', required : get_option('vulkan'))
endif
has_vulkan_header = cc.has_header('vulkan/vulkan.h')
if not has_vulkan_header and get_option('vulkan').enabled()
  error('vulkan plugin enabled, but vulkan.h not found')
endif

if vulkan_dep.found() and has_vulkan_header
  have_vulkan_windowing = false

  vkconf = configuration_data()

  xcb_dep = dependency('xcb', version : '>=1.10', required : get_option('x11'))
  if xcb_dep.found()
    vulkan_sources += [
      'xcb/vkdisplay_xcb.c',
      'xcb/vkwindow_xcb.c',
      'xcb/xcb_event_source.c',
    ]

    optional_deps += xcb_dep
    have_vulkan_windowing = true
    vkconf.set10('GST_VULKAN_HAVE_WINDOW_XCB', 1)
  endif

  wayland_client_dep = dependency('wayland-client', version : '>=1.4', required : get_option('wayland'))
  if wayland_client_dep.found()
    vulkan_sources += [
      'wayland/vkdisplay_wayland.c',
      'wayland/vkwindow_wayland.c',
      'wayland/wayland_event_source.c',
    ]

    optional_deps += wayland_client_dep
    have_vulkan_windowing = true
    vkconf.set10('GST_VULKAN_HAVE_WINDOW_WAYLAND', 1)
  endif

  if ['darwin', 'ios'].contains(host_system)
    objc = meson.get_compiler('objc')
    if not objc.has_argument('-fobjc-arc')
      error('ARC is required for building')
    endif

    vulkan_objc_args += ['-fobjc-arc']

    foundation_dep = dependency('appleframeworks', modules : ['Foundation'], required : get_option('vulkan'))
    quartzcore_dep = dependency('appleframeworks', modules : ['QuartzCore'], required : get_option('vulkan'))
    corefoundation_dep = dependency('appleframeworks', modules : ['CoreFoundation'], required : get_option('vulkan'))
    if foundation_dep.found() and quartzcore_dep.found() and corefoundation_dep.found()
      optional_deps += [foundation_dep, corefoundation_dep, quartzcore_dep]
    endif
  endif

  if host_system == 'darwin'
    cocoa_dep = dependency('appleframeworks', modules : ['Cocoa'], required : get_option('vulkan'))

    if cocoa_dep.found()
      vulkan_sources += [
        'cocoa/vkdisplay_cocoa.m',
        'cocoa/vkwindow_cocoa.m',
      ]
      optional_deps += [cocoa_dep]
      have_vulkan_windowing = true
      vkconf.set10('GST_VULKAN_HAVE_WINDOW_COCOA', 1)
    endif
  endif

  if host_system == 'ios'
    uikit_dep = dependency('appleframeworks', modules : ['UIKit'], required : get_option('vulkan'))

    if uikit_dep.found()
      vulkan_sources += [
        'ios/vkdisplay_ios.m',
        'ios/vkwindow_ios.m',
      ]
      optional_deps += [uikit_dep]
      have_vulkan_windowing = true
      vkconf.set10('GST_VULKAN_HAVE_WINDOW_IOS', 1)
    endif
  endif

  if have_vulkan_windowing
    configure_file(input : 'vkconfig.h.meson',
      output : 'vkconfig.h',
      configuration : vkconf)

    gstvulkan = library('gstvulkan',
      vulkan_sources,
      c_args : gst_plugins_bad_args + vulkan_defines,
      objc_args : gst_plugins_bad_args + vulkan_defines + vulkan_objc_args,
      link_args : noseh_link_args,
      include_directories : [configinc],
      dependencies : [gstvideo_dep, gstbase_dep, vulkan_dep] + optional_deps,
      install : true,
      install_dir : plugins_install_dir,
    )
    pkgconfig.generate(gstvulkan, install_dir : plugins_pkgconfig_install_dir)
  endif
endif

