executable('gst-plugin-scanner',
  'gst-plugin-scanner.c',
  c_args : gst_c_args,
  include_directories : [configinc],
  dependencies : [gobject_dep, gmodule_dep, glib_dep, mathlib, gst_dep],
  install_dir : helpers_install_dir,
  install: true,
)

# Used in test env setup to make tests find plugin scanner in build tree
gst_scanner_dir = meson.current_build_dir()

if bashcomp_found
  executable('gst-completion-helper',
    'gst-completion-helper.c',
    c_args : gst_c_args,
    include_directories : [configinc],
    dependencies : [gobject_dep, glib_dep, gst_dep],
    install_dir : helpers_install_dir,
    install: true,
  )
endif

# Check PTP support
have_ptp = false
if host_system == 'android'
  message('PTP not supported on Android because of permissions.')
elif host_system == 'windows'
  message('PTP not supported on Windows, not ported yet.')
elif host_system == 'ios'
  message('PTP not supported on iOS because of permissions.')
elif ['linux', 'darwin', 'netbsd', 'freebsd', 'openbsd', 'kfreebsd', 'dragonfly', 'sunos', 'gnu', 'gnu/kfreebsd'].contains(host_system)
  message('PTP supported on ' + host_system + '.')
  have_ptp = true
else
  message('PTP not supported on ' + host_system + ', not ported yet.')
endif

if have_ptp
  cdata.set('HAVE_PTP', 1, description : 'PTP support available')

  if cc.compiles('''#include <sys/ioctl.h>
                    #include <net/if.h>
                    int some_func (void) {
                      struct ifreq ifr;
                      struct ifconf ifc;
                      ioctl(0, SIOCGIFCONF, &ifc);
                      ioctl(0, SIOCGIFFLAGS, &ifr);
                      ioctl(0, SIOCGIFHWADDR, &ifr);
                      return ifr.ifr_hwaddr.sa_data[0];
                    }''',
                 name : 'SIOCGIFCONF, SIOCGIFFLAGS and SIOCGIFHWADDR available')
    cdata.set('HAVE_SIOCGIFCONF_SIOCGIFFLAGS_SIOCGIFHWADDR', 1,
      description : 'SIOCGIFCONF, SIOCGIFFLAGS and SIOCGIFHWADDR is available')
  endif

  if cc.compiles('''#include <ifaddrs.h>
                    #include <net/if.h>
                    #include <net/if_dl.h>
                    int some_func (void) {
                      struct ifaddrs *ifaddr;
                      getifaddrs(&ifaddr);
                      return (ifaddr->ifa_flags & IFF_LOOPBACK) && ifaddr->ifa_addr->sa_family != AF_LINK;
                    }''', name : 'getifaddrs() and AF_LINK available')
    cdata.set('HAVE_GETIFADDRS_AF_LINK', 1,
      description : 'getifaddrs() and AF_LINK is available')
  endif

  setcap_prog = find_program('setcap', '/usr/sbin/setcap', '/sbin/setcap', required : false)
  cap_dep = dependency('libcap', required: false)

  # user/group to change to in gst-ptp-helper
  ptp_helper_setuid_user = get_option('ptp-helper-setuid-user')
  if ptp_helper_setuid_user != ''
    cdata.set_quoted('HAVE_PTP_HELPER_SETUID_USER', ptp_helper_setuid_user,
      description : 'PTP helper setuid user')
  endif
  ptp_helper_setuid_group = get_option('ptp-helper-setuid-group')
  if ptp_helper_setuid_group != ''
    cdata.set_quoted('HAVE_PTP_HELPER_SETUID_GROUP', ptp_helper_setuid_group,
      description : 'PTP helper setuid group')
  endif

  # how to install gst-ptp-helper
  with_ptp_helper_permissions = get_option('ptp-helper-permissions')
  if with_ptp_helper_permissions == 'auto'
    if setcap_prog.found() and cap_dep.found()
      with_ptp_helper_permissions = 'capabilities'
    else
      with_ptp_helper_permissions = 'setuid-root'
    endif
  endif
  message('How to install gst-ptp-helper: ' + with_ptp_helper_permissions)

  if with_ptp_helper_permissions == 'none'
    # nothing to do
  elif with_ptp_helper_permissions == 'setuid-root'
    cdata.set('HAVE_PTP_HELPER_SETUID', 1,
        description : 'Use setuid-root for permissions in PTP helper')
  elif with_ptp_helper_permissions == 'capabilities'
    if not setcap_prog.found()
      error('capabilities-based ptp-helper-permissions requested, but could not find setcap tool.')
    elif not cap_dep.found()
      error('capabilities-based ptp-helper-permissions requested, but could not find libcap.')
    endif
    cdata.set('HAVE_PTP_HELPER_CAPABILITIES', 1,
        description : 'Use capabilities for permissions in PTP helper')
  else
    error('Unexpected ptp helper permissions value: ' + with_ptp_helper_permissions)
  endif

  executable('gst-ptp-helper', 'gst-ptp-helper.c',
    c_args : gst_c_args,
    include_directories : [configinc, libsinc],
    dependencies : [gio_dep, gobject_dep, glib_dep, mathlib, gst_dep, cap_dep],
    install_dir : helpers_install_dir,
    install : true)

  meson.add_install_script('ptp_helper_post_install.sh',
      helpers_install_dir, with_ptp_helper_permissions,
      setcap_prog.found() ? setcap_prog.path() : '')
endif

install_data(['gst_gdb.py', 'glib_gobject_helper.py'],
  install_dir : join_paths(get_option('datadir'), 'gstreamer-@0@'.format(apiversion), 'gdb'))

gdbconf = configuration_data()
gdbconf.set('GST_API_VERSION', apiversion)
gdbconf.set('DATADIR', '@0@/@1@'.format(get_option('prefix'), get_option('datadir')))

if host_system != 'windows'
  # XXX: We add a leading './' because prefix is an absolute path and we
  # need it to be a relative path so that join_paths appends it to the end.
  gdb_install_dir = join_paths(get_option('datadir'), 'gdb', 'auto-load', './' + get_option('prefix'), get_option('libdir'))
else
  # FIXME: Cannot install on Windows because the path will contain a drive
  # letter and colons are not allowed in paths.
  gdb_install_dir = disabler()
endif
configure_file(input : 'libgstreamer-gdb.py.in',
  output : 'libgstreamer-@0@.so.@1@-gdb.py'.format(apiversion, libversion),
  install_dir : gdb_install_dir,
  configuration : gdbconf)
