install_subdir('GstDebugViewer', install_dir: python3.sysconfig_path('purelib'),
    exclude_files: ['__init__.py'])
message('Installing in ' + python3.sysconfig_path('purelib'))

if find_program('msgfmt', required : get_option('nls')).found()
  # Desktop launcher and description file.
  desktop_file = i18n.merge_file(
    input: 'org.freedesktop.GstDebugViewer.desktop.in',
    output: 'org.freedesktop.GstDebugViewer.desktop',
    type: 'desktop',
    po_dir: 'po',
    install: true,
    install_dir: join_paths(get_option('datadir'), 'applications'),
  )

  # Appdata file.
  appdata_file = i18n.merge_file(
    input: 'org.freedesktop.GstDebugViewer.appdata.xml.in',
    output: 'org.freedesktop.GstDebugViewer.appdata.xml',
    po_dir: 'po',
    install: true,
    install_dir: join_paths(get_option('datadir'), 'metainfo'),
  )
else
  install_data('org.freedesktop.GstDebugViewer.desktop.in',
    rename: 'org.freedesktop.GstDebugViewer.desktop',
    install_dir: join_paths(get_option('datadir'), 'applications'))
  install_data('org.freedesktop.GstDebugViewer.appdata.xml.in',
    rename: 'org.freedesktop.GstDebugViewer.appdata.xml',
    install_dir: join_paths(get_option('datadir'), 'metainfo'))
endif

cdata = configuration_data()
cdata.set('LIBDIR', join_paths(get_option('prefix'), get_option('libdir')))
cdata.set('DATADIR', join_paths(get_option('prefix'), get_option('datadir')))
cdata.set('VERSION', meson.project_version())

configure_file(input: 'gst-debug-viewer',
               output: 'gst-debug-viewer',
               configuration: cdata,
               install_dir: get_option('bindir'))

configure_file(input: 'GstDebugViewer/__init__.py',
               output: '__init__.py',
               configuration: cdata,
               install_dir: join_paths(python3.sysconfig_path('purelib'), 'GstDebugViewer'))

pkgdatadir = join_paths(get_option('datadir'), meson.project_name())
icondir = join_paths(get_option('datadir'), 'icons/hicolor')

subdir('data')


if run_command(python3.find_python(),
    '-c', 'import gi; gi.require_version("Gtk", "3.0")').returncode() == 0
  test('gst-debug-viewer', python3.find_python(), args: ['-m', 'unittest'],
      workdir: meson.current_source_dir())
endif