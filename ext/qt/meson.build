sources = [
  'gstplugin.cc',
  'gstqsgtexture.cc',
  'gstqtglutility.cc',
  'gstqtsink.cc',
  'gstqtsrc.cc',
  'qtitem.cc',
  'qtwindow.cc',
]

moc_headers = [
  'qtitem.h',
  'qtwindow.h',
  'gstqsgtexture.h',
]

# FIXME: -Dqt5=enabled is silently ignored if a c++ compiler is not found
if have_cxx and build_gstgl
  qt5_mod = import('qt5')
  qt5qml_dep = dependency('qt5', modules : ['Core', 'Gui', 'Qml', 'Quick'],
                          required: get_option('qt5'))

  # FIXME Add a way to get that information out of the qt5 module
  moc = find_program('moc-qt5', 'moc', required : get_option('qt5'))
  if qt5qml_dep.found() and moc.found()
    optional_deps = []
    qt_defines = []
    have_qpa_include = false
    have_qt_windowing = false

    # Attempt to find the QPA header either through pkg-config (preferred) or qmake
    # This semi-matches what meson does internally with the qt5 module
    # FIXME Add a way to get some of this information out of the qt5 module
    if not have_qpa_include
      # FIXME: automagic
      qt5core_dep = dependency('Qt5Core', required: false)
      if qt5core_dep.found() and qt5core_dep.type_name() == 'pkgconfig'
        qt_version = qt5core_dep.version()
        qt_include_dir = qt5core_dep.get_pkgconfig_variable('includedir')
        qpa_include_path = join_paths(qt_include_dir, 'QtGui', qt_version, 'QtGui')
        if cxx.has_header('qpa/qplatformnativeinterface.h',
                dependencies : qt5core_dep,
                args : '-I' + qpa_include_path)
          qt_defines += '-DHAVE_QT_QPA_HEADER'
          qt_defines += '-I' + qpa_include_path
          have_qpa_include = true
          message('Found QPA header using pkg-config')
        endif
      endif
    endif
    if not have_qpa_include
      qmake = find_program('qmake')
      if qmake.found()
        qt_version = run_command(qmake, '-query', 'QT_VERSION').stdout().strip()
        qt_include_dir = run_command(qmake, '-query', 'QT_INSTALL_HEADERS').stdout().strip()
        qpa_include_path = join_paths(qt_include_dir, 'QtGui', qt_version, 'QtGui')
        if cxx.has_header('qpa/qplatformnativeinterface.h',
                args : '-I' + qpa_include_path)
          qt_defines += '-DHAVE_QT_QPA_HEADER'
          qt_defines += '-I' + qpa_include_path
          have_qpa_include = true
          message('Found QPA header using qmake')
        endif
      endif
    endif

    # Try to come up with all the platform/winsys combinations that will work

    if gst_gl_have_window_x11 and gst_gl_have_platform_glx
      # FIXME: automagic
      qt5x11extras = dependency('qt5', modules : ['X11Extras'], required : false)
      if qt5x11extras.found()
        optional_deps += qt5x11extras
        qt_defines += ['-DHAVE_QT_X11']
        have_qt_windowing = true
      endif
    endif

    if gst_gl_have_platform_egl
      if have_qpa_include
        if gst_gl_have_window_wayland
          # FIXME: automagic
          qt5waylandextras = dependency('qt5', modules : ['WaylandClient'], required : false)
          if qt5waylandextras.found()
            optional_deps += qt5waylandextras
            qt_defines += ['-DHAVE_QT_WAYLAND']
            have_qt_windowing = true
          endif
        endif
        if gst_gl_have_window_android
          # FIXME: automagic
          # FIXME: untested
          qt5androidextras = dependency('qt5', modules : ['AndroidExtras'], required : false)
          if qt5androidextras.found()
            optional_deps += qt5androidextras
            # also uses the HAVE_QT_EGLFS define below
            have_qt_windowing = true
          endif
        endif
      endif
      qt_defines += ['-DHAVE_QT_EGLFS']
      have_qt_windowing = true
    endif

    if gst_gl_have_platform_wgl and gst_gl_have_window_win32
      # for wglMakeCurrent()
      # FIXME: automagic
      opengl32_dep = cc.find_library('opengl32', required : false)
      if opengl32_dep.found()
        qt_defines += ['-DHAVE_QT_WIN32']
        optional_deps += opengl32_dep
        have_qt_windowing = true
      endif
    endif

    # FIXME: OSX/iOS definitions

    if have_qt_windowing
      # Build it!
      moc_files = qt5_mod.preprocess(moc_headers : moc_headers)
      library('gstqmlgl', sources, moc_files,
        cpp_args : gst_plugins_good_args + qt_defines,
        link_args : noseh_link_args,
        include_directories: [configinc, libsinc],
        dependencies : [glib_deps, gst_dep, gstvideo_dep, gstgl_dep, qt5qml_dep, optional_deps],
        install: true,
        install_dir : plugins_install_dir)
    endif
  endif
endif
