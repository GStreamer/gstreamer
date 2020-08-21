gstvulkan_dep = dependency('', required: false)
gstvulkanxcb_dep = dependency('', required: false)
gstvulkanwyland_dep = dependency('', required: false)
if get_option('vulkan').disabled()
  subdir_done()
endif

vulkan_sources = [
  'gstvkbuffermemory.c',
  'gstvkbufferpool.c',
  'gstvkcommandbuffer.c',
  'gstvkcommandpool.c',
  'gstvkdescriptorcache.c',
  'gstvkdescriptorset.c',
  'gstvkdescriptorpool.c',
  'gstvkdevice.c',
  'gstvkdebug.c',
  'gstvkdisplay.c',
  'gstvkerror.c',
  'gstvkfence.c',
  'gstvkformat.c',
  'gstvkfullscreenquad.c',
  'gstvkhandle.c',
  'gstvkhandlepool.c',
  'gstvkimagememory.c',
  'gstvkimagebufferpool.c',
  'gstvkimageview.c',
  'gstvkinstance.c',
  'gstvkmemory.c',
  'gstvkphysicaldevice.c',
  'gstvkqueue.c',
  'gstvkswapper.c',
  'gstvktrash.c',
  'gstvkvideofilter.c',
  'gstvkutils.c',
  'gstvkwindow.c',
]

vulkan_headers = [
  'gstvkapi.h',
  'gstvkbarrier.h',
  'gstvkbuffermemory.h',
  'gstvkbufferpool.h',
  'gstvkcommandbuffer.h',
  'gstvkcommandpool.h',
  'gstvkdescriptorcache.h',
  'gstvkdescriptorset.h',
  'gstvkdescriptorpool.h',
  'gstvkdebug.h',
  'gstvkdevice.h',
  'gstvkdisplay.h',
  'gstvkerror.h',
  'gstvkfence.h',
  'gstvkformat.h',
  'gstvkfullscreenquad.h',
  'gstvkhandle.h',
  'gstvkhandlepool.h',
  'gstvkimagememory.h',
  'gstvkimagebufferpool.h',
  'gstvkimageview.h',
  'gstvkinstance.h',
  'gstvkmemory.h',
  'gstvkphysicaldevice.h',
  'gstvkqueue.h',
  'gstvkswapper.h',
  'gstvktrash.h',
  'gstvkutils.h',
  'gstvkvideofilter.h',
  'gstvkwindow.h',
  'vulkan-prelude.h',
  'vulkan_fwd.h',
  'vulkan.h',
]

vulkan_priv_sources = []
vulkan_xcb_sources = []
vulkan_xcb_headers = []
vulkan_wayland_sources = []
vulkan_wayland_headers = []

vulkan_windowing = false
vulkan_objc_args = []
vulkan_defines = []
optional_deps = []
has_vulkan_header = false
vulkan_dep = dependency('', required: false)
vulkan_inc_dir = ''
enabled_vulkan_winsys = []

vulkan_conf = configuration_data()
vulkan_conf_options = [
    'GST_VULKAN_HAVE_WINDOW_XCB',
    'GST_VULKAN_HAVE_WINDOW_WAYLAND',
    'GST_VULKAN_HAVE_WINDOW_COCOA',
    'GST_VULKAN_HAVE_WINDOW_IOS',
    'GST_VULKAN_HAVE_WINDOW_WIN32',
    'GST_VULKAN_HAVE_WINDOW_ANDROID',
]

foreach option : vulkan_conf_options
  vulkan_conf.set10(option, false)
endforeach

if ['ios', 'darwin'].contains(host_system)
  # - ios does not support the loader/validation layers
  # - We need to link directly to MoltenVK to be able to use
  #   MoltenVK-specific functions that use dispatchable handles (like
  #   retrieving the metal device from the VkDevice) which is currently waiting
  #   on implementing a proper Metal extension for Vulkan
  #   https://github.com/KhronosGroup/MoltenVK/issues/492
  vulkan_dep = cc.find_library('MoltenVK', required : get_option('vulkan'))
elif host_system == 'windows'
  vulkan_root = run_command(python3, '-c', 'import os; print(os.environ.get("VK_SDK_PATH"))').stdout().strip()
  if vulkan_root != '' and vulkan_root != 'None'
    vulkan_lib_dir = ''
    if build_machine.cpu_family() == 'x86_64'
      vulkan_lib_dir = join_paths(vulkan_root, 'Lib')
    else
      vulkan_lib_dir = join_paths(vulkan_root, 'Lib32')
    endif

    vulkan_lib = cc.find_library('vulkan-1', dirs: vulkan_lib_dir,
                                 required : get_option('vulkan'))

    vulkan_inc_dir = join_paths(vulkan_root, 'Include')
    has_vulkan_header = cc.has_header('vulkan/vulkan_core.h',
                                      args: '-I' + vulkan_inc_dir)

    if vulkan_lib.found() and has_vulkan_header
      vulkan_dep = declare_dependency(include_directories: include_directories(vulkan_inc_dir),
                                      dependencies: vulkan_lib)
    endif
  endif
else
  vulkan_dep = dependency('vulkan', method: 'pkg-config', required : false)
  if not vulkan_dep.found()
    vulkan_dep = cc.find_library('vulkan', required : false)
  endif
endif

if host_system != 'windows'
  has_vulkan_header = cc.has_header('vulkan/vulkan_core.h')
endif

if not has_vulkan_header and get_option('vulkan').enabled()
  error('vulkan plugin enabled, but vulkan.h not found')
endif
if not vulkan_dep.found() and get_option('vulkan').enabled()
  error('vulkan plugin enabled, but could not find vulkan library')
endif

xcb_dep = dependency('xcb', version : '>=1.10', required : get_option('x11'))
xkbcommon_dep = dependency('xkbcommon', required : get_option('x11'))
xkbcommon_x11_dep = dependency('xkbcommon-x11', required : get_option('x11'))

if xcb_dep.found() and xkbcommon_dep.found() and xkbcommon_x11_dep.found() and cc.has_header('vulkan/vulkan_xcb.h', dependencies : vulkan_dep)
  vulkan_priv_sources += [
    'xcb/gstvkwindow_xcb.c',
    'xcb/xcb_event_source.c',
  ]
  vulkan_xcb_sources += [
    'xcb/gstvkdisplay_xcb.c',
  ]
  vulkan_xcb_headers += [
    'xcb/xcb.h',
    'xcb/gstvkdisplay_xcb.h'
  ]

  optional_deps += [xcb_dep, xkbcommon_dep, xkbcommon_x11_dep]
  vulkan_windowing = true
  vulkan_conf.set10('GST_VULKAN_HAVE_WINDOW_XCB', 1)
  enabled_vulkan_winsys += ['xcb']
endif

wayland_client_dep = dependency('wayland-client', version : '>=1.4', required : get_option('wayland'))
if wayland_client_dep.found() and cc.has_header('vulkan/vulkan_wayland.h', dependencies : vulkan_dep)
  vulkan_priv_sources += [
    'wayland/gstvkdisplay_wayland.c',
    'wayland/gstvkwindow_wayland.c',
    'wayland/wayland_event_source.c',
  ]
  vulkan_wayland_sources += [
    'wayland/gstvkdisplay_wayland.c',
  ]
  vulkan_wayland_headers += [
    'wayland/wayland.h',
    'wayland/gstvkdisplay_wayland.h'
  ]

  optional_deps += wayland_client_dep
  vulkan_windowing = true
  vulkan_conf.set10('GST_VULKAN_HAVE_WINDOW_WAYLAND', 1)
  enabled_vulkan_winsys += ['wayland']
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

  if cocoa_dep.found() and cc.has_header('vulkan/vulkan_macos.h', dependencies : vulkan_dep)
    vulkan_priv_sources += [
      'cocoa/gstvkdisplay_cocoa.m',
      'cocoa/gstvkwindow_cocoa.m',
    ]
    optional_deps += [cocoa_dep]
    vulkan_windowing = true
    vulkan_conf.set10('GST_VULKAN_HAVE_WINDOW_COCOA', 1)
    enabled_vulkan_winsys += ['cocoa']
  endif
endif

if host_system == 'ios'
  uikit_dep = dependency('appleframeworks', modules : ['UIKit'], required : get_option('vulkan'))

  if uikit_dep.found() and cc.has_header('vulkan/vulkan_ios.h', dependencies : vulkan_dep)
    vulkan_priv_sources += [
      'ios/gstvkdisplay_ios.m',
      'ios/gstvkwindow_ios.m',
    ]
    optional_deps += [uikit_dep]
    vulkan_windowing = true
    vulkan_conf.set10('GST_VULKAN_HAVE_WINDOW_IOS', 1)
    enabled_vulkan_winsys += ['ios']
  endif
endif

if host_system == 'windows'
  gdi_dep = cc.find_library('gdi32', required : get_option('vulkan'))

  # Cannot use internal dependency object with cc.has_header()
  if gdi_dep.found() and cc.has_header('vulkan/vulkan_win32.h', args: '-I' + vulkan_inc_dir)
    vulkan_priv_sources += ['win32/gstvkwindow_win32.c']
    optional_deps += [gdi_dep]
    vulkan_windowing = true
    vulkan_conf.set10('GST_VULKAN_HAVE_WINDOW_WIN32', 1)
    enabled_vulkan_winsys += ['win32']
  endif
endif

if host_system == 'android'
  if cc.has_header('vulkan/vulkan_android.h', dependencies : vulkan_dep)
    vulkan_priv_sources += [
      'android/gstvkdisplay_android.c',
      'android/gstvkwindow_android.c',
    ]
    vulkan_windowing = true
    vulkan_conf.set10('GST_VULKAN_HAVE_WINDOW_ANDROID', 1)
    enabled_vulkan_winsys += ['android']
  endif
endif

if not vulkan_windowing
  warning('No Windowing system found.  vulkansink will not work')
endif

# Only needed for the vulkan plugin, but doesn't make sense to build
# anything else vulkan related if we are not going to build the plugin
glslc = find_program('glslc', required: get_option('vulkan'))

if not vulkan_dep.found() or not has_vulkan_header or not glslc.found()
  if get_option('vulkan').enabled()
    error('GStreamer Vulkan integration required via options, but needed dependencies not found.')
  else
    subdir_done()
  endif
endif

gen_sources = []

install_headers(vulkan_headers, subdir : 'gstreamer-1.0/gst/vulkan')

configure_file(input : 'gstvkconfig.h.meson',
  output : 'gstvkconfig.h',
  install_dir : join_paths(get_option('includedir'), 'gstreamer-1.0/gst/vulkan'),
  configuration : vulkan_conf)

glib_mkenums = find_program('glib-mkenums')
mkenums = find_program('vulkan_mkenum.py')
vulkan_enumtypes_h = custom_target('gstvulkanenum_h',
  output : 'vulkan-enumtypes.h',
  input : vulkan_headers,
  install : true,
  install_dir : join_paths(get_option('includedir'), 'gstreamer-1.0/gst/vulkan/'),
  command : [mkenums, glib_mkenums, '@OUTPUT@', '@INPUT@'])

vulkan_enumtypes_c = custom_target('gstvulkanenum_c',
  output : 'vulkan-enumtypes.c',
  input : vulkan_headers,
  depends : [vulkan_enumtypes_h],
  command : [mkenums, glib_mkenums, '@OUTPUT@', '@INPUT@'])
gen_sources += [vulkan_enumtypes_h]

gstvulkan = library('gstvulkan-' + api_version,
  vulkan_sources, vulkan_priv_sources, vulkan_wayland_sources, vulkan_xcb_sources, vulkan_enumtypes_c, vulkan_enumtypes_h,
  c_args : gst_plugins_bad_args + vulkan_defines + ['-DBUILDING_GST_VULKAN'],
  objc_args : gst_plugins_bad_args + vulkan_defines + vulkan_objc_args + ['-DBUILDING_GST_VULKAN'],
  include_directories : [configinc, libsinc],
  version : libversion,
  soversion : soversion,
  darwin_versions : osxversion,
  install : true,
  dependencies : [gstbase_dep, gstvideo_dep, vulkan_dep] + optional_deps,
  # don't confuse gst/vulkan/xcb/xcb.h with xcb/xcb.h
  implicit_include_directories : false)

if build_gir
  extra_gir_includes = []
  gobject_introspection_dep = dependency('gobject-introspection-1.0')
  if gobject_introspection_dep.version().version_compare('>=1.61.1')
    # This is the first version that contains Vulkan-1.0.gir
    extra_gir_includes += ['Vulkan-1.0']
  endif

  vulkan_gir = gnome.generate_gir(gstvulkan,
    sources : vulkan_sources + vulkan_headers + [vulkan_enumtypes_h, vulkan_enumtypes_c],
    namespace : 'GstVulkan',
    nsversion : api_version,
    identifier_prefix : 'Gst',
    symbol_prefix : 'gst',
    export_packages : 'gstreamer-vulkan-1.0',
    includes : ['Gst-1.0', 'GstBase-1.0', 'GstVideo-1.0'] + extra_gir_includes,
    install : true,
    extra_args : gir_init_section + ['--c-include=gst/vulkan/vulkan.h'],
    dependencies : [gstvideo_dep, gst_dep, gstbase_dep] + optional_deps
  )
  gen_sources += vulkan_gir
endif

gstvulkan_dep = declare_dependency(link_with : gstvulkan,
  include_directories : [libsinc],
  sources: gen_sources,
  dependencies : [gstvideo_dep, gstbase_dep, vulkan_dep] + optional_deps)

if enabled_vulkan_winsys.contains('xcb')
  install_headers(vulkan_xcb_headers, subdir : 'gstreamer-1.0/gst/vulkan/xcb')
  vulkan_xcb_gir = []
  if build_gir
    vulkan_xcb_gir = gnome.generate_gir(gstvulkan,
      sources : vulkan_xcb_sources + vulkan_xcb_headers,
      namespace : 'GstVulkanXCB',
      nsversion : api_version,
      identifier_prefix : 'Gst',
      symbol_prefix : 'gst',
      export_packages : 'gstreamer-vulkan-xcb-1.0',
      includes : ['Gst-1.0', 'GstBase-1.0', 'GstVideo-1.0', vulkan_gir[0]] + extra_gir_includes,
      install : true,
      extra_args : gir_init_section + ['--c-include=gst/vulkan/xcb/xcb.h'],
      dependencies : [gstvideo_dep, gst_dep, gstbase_dep] + optional_deps
    )
  endif
  gstvulkanxcb_dep = declare_dependency(dependencies : [gstvulkan_dep],
      sources : vulkan_xcb_gir)
endif

if enabled_vulkan_winsys.contains('wayland')
  install_headers(vulkan_wayland_headers, subdir : 'gstreamer-1.0/gst/vulkan/wayland')
  vulkan_wayland_gir = []
  if build_gir
    vulkan_wayland_gir = gnome.generate_gir(gstvulkan,
      sources : vulkan_wayland_sources + vulkan_wayland_headers,
      namespace : 'GstVulkanWayland',
      nsversion : api_version,
      identifier_prefix : 'Gst',
      symbol_prefix : 'gst',
      export_packages : 'gstreamer-vulkan-wayland-1.0',
      includes : ['Gst-1.0', 'GstBase-1.0', 'GstVideo-1.0', vulkan_gir[0]] + extra_gir_includes,
      install : true,
      extra_args : gir_init_section + ['--c-include=gst/vulkan/wayland/wayland.h'],
      dependencies : [gstvideo_dep, gst_dep, gstbase_dep] + optional_deps
    )
  endif
  gstvulkanwayland_dep = declare_dependency(dependencies : [gstvulkan_dep],
      sources : vulkan_wayland_gir)
endif
