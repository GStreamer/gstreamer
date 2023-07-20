sources = [
  'gstplugin.cc',
  'gstqtelement.cc',
  'gstqsgmaterial.cc',
  'gstqtglutility.cc',
  'gstqtoverlay.cc',
  'gstqtsink.cc',
  'gstqtsrc.cc',
  'qtglrenderer.cc',
  'qtitem.cc',
  'qtwindow.cc',
]

moc_headers = [
  'qtitem.h',
  'qtwindow.h',
  'qtglrenderer.h',
]

# Define a not-found dependency so this variable is always defined when we're
# deciding whether to build the qt5 examples
qt5qml_dep = dependency('', required: false)
qt5_option = get_option('qt5')
qt5_egl = get_option('qt-egl')
qt5_wayland = get_option('qt-wayland')
qt5_x11 = get_option('qt-x11')
qt5_method = get_option('qt-method')

if qt5_option.disabled()
  subdir_done()
endif

if not have_gstgl
  if qt5_option.enabled()
    error('qt5 qmlglsink plugin is enabled, but gstreamer-gl-1.0 was not found')
  endif
  subdir_done()
endif

if not add_languages('cpp', native: false, required: qt5_option)
  subdir_done()
endif

qt5_mod = import('qt5')
if not qt5_mod.has_tools(method: qt5_method)
  if qt5_option.enabled()
    error('qt5 qmlglsink plugin is enabled, but qt specific tools were not found')
  endif
  subdir_done()
endif

qt5qml_dep = dependency('qt5', modules : ['Core', 'Gui', 'Qml', 'Quick'],
                        method: qt5_method, required: qt5_option, static: host_system == 'ios')
if not qt5qml_dep.found()
  subdir_done()
endif

optional_deps = []
qt_defines = []
have_qpa_include = false
have_qt_windowing = false

# Look for the QPA platform native interface header
qpa_header_path = join_paths(qt5qml_dep.version(), 'QtGui')
qpa_header = join_paths(qpa_header_path, 'qpa/qplatformnativeinterface.h')
need_qpa_include = qt5_option.enabled() and (host_system == 'android' or qt5_wayland.enabled())
if cxx.has_header(qpa_header, dependencies : qt5qml_dep, required: need_qpa_include)
  qt_defines += '-DHAVE_QT_QPA_HEADER'
  qt_defines += '-DQT_QPA_HEADER=' + '<@0@>'.format(qpa_header)
  have_qpa_include = true
  message('Found QtGui QPA header in ' + qpa_header_path)
endif

## Try to come up with all the platform/winsys combinations that will work

# X11 windowing
qt5_x11 = qt5_x11 \
    .require(gstglx11_dep.found(), error_message: 'gstreamer-gl-x11-1.0 is required') \
    .require(gst_gl_have_window_x11, error_message: 'x11 windowing support in gstreamer-gl is required') \
    .require(gst_gl_have_platform_glx, error_message: 'glx platform support in gstreamer-gl is required')
if qt5_x11.allowed()
  qt5x11extras = dependency('qt5', modules : ['X11Extras'], method: qt5_method, required: qt5_x11)
  if qt5x11extras.found()
    optional_deps += [qt5x11extras, gstglx11_dep]
    qt_defines += ['-DHAVE_QT_X11']
    have_qt_windowing = true
  endif
endif

# Wayland windowing
qt5_wayland = qt5_wayland \
    .require(gstglwayland_dep.found(), error_message: 'gstreamer-gl-wayland-1.0 is required') \
    .require(gst_gl_have_window_wayland, error_message: 'wayland windowing support in gstreamer-gl is required') \
    .require(gst_gl_have_platform_egl, error_message: 'egl platform support in gstreamer-gl is required') \
    .require(have_qpa_include, error_message: 'QPA platform native interface header is required')
if qt5_wayland.allowed()
  qt5waylandextras = dependency('qt5', modules : ['WaylandClient'], method: qt5_method, required: qt5_wayland)
  if qt5waylandextras.found()
    optional_deps += [qt5waylandextras, gstglwayland_dep]
    qt_defines += ['-DHAVE_QT_WAYLAND']
    have_qt_windowing = true
  endif
endif

# EGL windowing for Embedded linux (e.g. i.MX6) with or without windowing
# support
qt5_egl = qt5_egl \
    .require(host_system == 'linux') \
    .require(gstglegl_dep.found(), error_message: 'gstreamer-gl-egl-1.0 is required') \
    .require(gst_gl_have_platform_egl, error_message: 'egl platform support in gstreamer-gl is required')
if qt5_egl.allowed()
  qt_defines += ['-DHAVE_QT_EGLFS']
  optional_deps += gstglegl_dep
  have_qt_windowing = true

  # EGL windowing for Vivante Framebuffer (e.g. i.MX6)
  if gstglviv_fb_dep.found()
    qt_defines += ['-DHAVE_QT_VIV_FB']
    optional_deps += gstglviv_fb_dep
  endif
endif

# Android windowing
if host_system == 'android'
  qt5_android = qt5_option \
      .require(gst_gl_have_window_android, error_message: 'android windowing support in gstreamer-gl is required') \
      .require(gst_gl_have_platform_egl, error_message: 'egl platform support in gstreamer-gl is required')
  if gst_gl_have_window_android
    qt5androidextras = dependency('qt5', modules : ['AndroidExtras'], method: qt5_method, required : qt5_android)
    # for gl functions in QtGui/qopenglfunctions.h
    glesv2_dep = cc.find_library('GLESv2', required : qt5_android)
    if glesv2_dep.found() and qt5androidextras.found()
      optional_deps += [qt5androidextras, glesv2_dep]
      qt_defines += ['-DHAVE_QT_ANDROID']
      have_qt_windowing = true
    endif
  endif
endif

# Win32 windowing
if host_system == 'windows'
  qt5_win32 = qt5_option \
      .require(gst_gl_have_window_win32, error_message: 'win32 windowing support in gstreamer-gl is required') \
      .require(gst_gl_have_platform_wgl, error_message: 'wgl platform support in gstreamer-gl is required')
  if qt5_win32.allowed()
    # for wglMakeCurrent()
    opengl32_dep = cc.find_library('opengl32', required : qt5_win32)
    if opengl32_dep.found()
      qt_defines += ['-DHAVE_QT_WIN32']
      optional_deps += opengl32_dep
      have_qt_windowing = true
    endif
  endif
endif

# macOS windowing
if host_system == 'darwin'
  qt5_macos = qt5_option \
      .require(gst_gl_have_window_cocoa, error_message: 'cocoa windowing support in gstreamer-gl is required') \
      .require(gst_gl_have_platform_cgl, error_message: 'cgl platform support in gstreamer-gl is required')
  if qt5_macos.allowed()
    qt5macextras = dependency('qt5', modules : ['MacExtras'], method: qt5_method, required : qt5_macos)
    if qt5macextras.found()
      qt_defines += ['-DHAVE_QT_MAC']
      optional_deps += qt5macextras
      have_qt_windowing = true
    endif
  endif
endif

# iOS windowing
if host_system == 'ios'
  qt5_ios = qt5_option \
      .require(gst_gl_have_window_eagl, error_message: 'eagl windowing support in gstreamer-gl is required') \
      .require(gst_gl_have_platform_eagl, error_message: 'eagl platform support in gstreamer-gl is required')
  if qt5_ios.allowed()
    qt_defines += ['-DHAVE_QT_IOS']
    have_qt_windowing = true
  endif
endif

if qt5_option.require(have_qt_windowing).allowed()
  # rpath is needed to be able to load the plugin on macOS inside the devenv
  qmlgl_kwargs = {}
  if host_system == 'darwin'
    fs = import('fs')
    qt_bindir = fs.parent(find_program('qmake').full_path())
    qt_libdir = fs.parent(qt_bindir) / 'lib'
    qmlgl_kwargs += {'build_rpath': qt_libdir}
  endif

  # Build it!
  moc_files = qt5_mod.preprocess(moc_headers : moc_headers, method: qt5_method)
  gstqmlgl = library('gstqmlgl', sources, moc_files,
    cpp_args : gst_plugins_good_args + qt_defines,
    link_args : noseh_link_args,
    include_directories: [configinc, libsinc],
    dependencies : [gst_dep, gstvideo_dep, gstgl_dep, gstglproto_dep, qt5qml_dep, optional_deps],
    override_options : ['cpp_std=c++11'],
    kwargs: qmlgl_kwargs,
    install: true,
    install_dir : plugins_install_dir)
  plugins += [gstqmlgl]
endif
