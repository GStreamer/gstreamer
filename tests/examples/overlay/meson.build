if x11_dep.found() # FIXME: originally if USE_X
  if gtk_x11_dep.found()
    executable('gtk-videooverlay', 'gtk-videooverlay.c',
      c_args : gst_plugins_base_args,
      include_directories: [configinc, libsinc],
      dependencies : [glib_deps, gst_dep, video_dep, libm, x11_dep, gtk_x11_dep],
      install: false)
  endif

  if add_languages('cpp')                          # check for C++ support
    qt5_mod = import('qt5')
    qt5widgets_dep = dependency('qt5', modules : ['Gui', 'Widgets'], required: false)

    # FIXME Add a way to get that information out of the qt5 module
    moc = find_program('moc-qt5', required : false)
    if qt5widgets_dep.found() and moc.found()
      executable('qt-videooverlay', 'qt-videooverlay.cpp',
        cpp_args : gst_plugins_base_args,
        include_directories: [configinc, libsinc],
        dependencies : [glib_deps, gst_dep, video_dep, libm, x11_dep, qt5widgets_dep],
        install: false)

      # FIXME: this doesn't work properly yet, no moc_qtgv-videooverlay.cpp generated
      moc_files = qt5_mod.preprocess(moc_headers : 'qtgv-videooverlay.h')#moc_sources : 'qtgv-videooverlay.cpp')
      executable('qtgv-videooverlay', 'qtgv-videooverlay.cpp', moc_files,
        cpp_args : gst_plugins_base_args,
        include_directories: [configinc, libsinc],
        dependencies : [glib_deps, gst_dep, video_dep, libm, x11_dep, qt5widgets_dep],
        install: false)
    endif
  endif
endif
