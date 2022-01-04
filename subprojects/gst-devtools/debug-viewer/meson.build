python3.install_sources (
    'GstDebugViewer/Main.py',
    'GstDebugViewer/Data.py',
    subdir: 'GstDebugViewer')

python3.install_sources (
    'GstDebugViewer/GUI/columns.py',
    'GstDebugViewer/GUI/__init__.py',
    'GstDebugViewer/GUI/models.py',
    'GstDebugViewer/GUI/filters.py',
    'GstDebugViewer/GUI/colors.py',
    'GstDebugViewer/GUI/window.py',
    'GstDebugViewer/GUI/app.py',
    subdir: 'GstDebugViewer/GUI')

python3.install_sources (
    'GstDebugViewer/Plugins/__init__.py',
    'GstDebugViewer/Plugins/FindBar.py',
    'GstDebugViewer/Plugins/Timeline.py',
    subdir: 'GstDebugViewer/Plugins')

python3.install_sources (
    'GstDebugViewer/Common/Main.py',
    'GstDebugViewer/Common/utils.py',
    'GstDebugViewer/Common/__init__.py',
    'GstDebugViewer/Common/generictreemodel.py',
    'GstDebugViewer/Common/Data.py',
    'GstDebugViewer/Common/GUI.py',
    subdir: 'GstDebugViewer/Common')

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

init_file = configure_file(
    input: 'GstDebugViewer/__init__.py',
    output: '__init__.py',
    configuration: cdata)
python3.install_sources (init_file, subdir: 'GstDebugViewer')

pkgdatadir = join_paths(get_option('datadir'), meson.project_name())
icondir = join_paths(get_option('datadir'), 'icons/hicolor')

subdir('data')


if run_command(python3, '-c', 'import gi; gi.require_version("Gtk", "3.0")', check: false).returncode() == 0
  test('gst-debug-viewer', python3, args: ['-m', 'unittest'],
      workdir: meson.current_source_dir())
endif
