if x11_dep.found() and gtk_x11_dep.found()
    executable('gtk-videooverlay', 'gtk-videooverlay.c',
      c_args : gst_plugins_base_args,
      include_directories: [configinc, libsinc],
      dependencies : [gst_dep, video_dep, libm, x11_dep, gtk_x11_dep],
      install: false)
endif

if qt5gui_dep.found()
  executable('qt-videooverlay', 'qt-videooverlay.cpp',
    cpp_args : gst_plugins_base_args,
    include_directories: [configinc, libsinc],
    dependencies: [gst_dep, video_dep, libm, x11_dep, qt5gui_dep],
    install: false)

  if qt5_have_tools
    # FIXME: this doesn't work properly yet, no moc_qtgv-videooverlay.cpp generated
    # moc_sources : 'qtgv-videooverlay.cpp'
    moc_files = qt5_mod.preprocess(moc_headers: 'qtgv-videooverlay.h', method: qt5_method)
    executable('qtgv-videooverlay', 'qtgv-videooverlay.cpp', moc_files,
      cpp_args : gst_plugins_base_args,
      include_directories: [configinc, libsinc],
      dependencies : [gst_dep, video_dep, libm, x11_dep, qt5gui_dep],
      install: false)
  endif
endif

if host_system == 'windows'
  executable('win32-videooverlay', 'win32-videooverlay.c',
    c_args : gst_plugins_base_args,
    include_directories : [configinc, libsinc],
    dependencies : [gst_dep, video_dep],
    install: false,
  )

  executable('win32-videooverlay-playbin', 'win32-videooverlay-playbin.c',
    c_args : gst_plugins_base_args,
    include_directories : [configinc, libsinc],
    dependencies : [gst_dep, video_dep],
    install: false,
  )
endif
