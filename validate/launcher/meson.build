_launcherdir = get_option('libdir') + '/gst-validate-launcher/python/launcher/'

launcher_configure = configuration_data()
launcher_configure.set('GST_VALIDATE_TESTSUITE_VERSION', '"@0@"'.format(TESTUITE_VERSION))
configure_file(input : 'config.py.in',
  output : 'config.py',
  install_dir: _launcherdir,
  configuration : launcher_configure)

_sources = ['baseclasses.py',
            '__init__.py',
            'loggable.py',
            'reporters.py',
            'main.py',
            'httpserver.py',
            'RangeHTTPServer.py',
            'utils.py',
            'vfb_server.py']

install_data(sources: _sources,
             install_dir: _launcherdir)

subdir('apps')
