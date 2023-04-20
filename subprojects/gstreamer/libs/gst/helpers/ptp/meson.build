# Check PTP support
ptp_helper_option = get_option('ptp-helper')

if ptp_helper_option.disabled()
  subdir_done()
endif

if host_system not in ['linux', 'freebsd', 'openbsd', 'netbsd', 'dragonfly', 'darwin', 'sunos', 'solaris', 'illumos', 'windows']
  if ptp_helper_option.enabled()
    error('PTP not supported on this OS')
  endif
  subdir_done()
endif

have_rust = add_languages('rust', native : false, required : false)
if not have_rust
  if ptp_helper_option.enabled()
    error('PTP not supported without Rust compiler')
  endif
  subdir_done()
endif

rustc = meson.get_compiler('rust')

if rustc.get_id() not in ['rustc', 'clippy-driver rustc']
  warning('PTP support is only tested with rustc, found different compiler @0@ @1@'.format(rustc.get_id(), rustc.version()))
endif

# We currently need at least Rust 1.48 on all platforms but Windows.
# On Windows some 1.54 API is used that would otherwise complicate things
# unncecessarily.
rust_req = '1.48'
if host_system == 'windows'
  rust_req = '1.54'
endif

if rustc.get_id() in ['rustc', 'clippy-driver rustc'] and not rustc.version().version_compare('>=' + rust_req)
  if ptp_helper_option.enabled()
    error('PTP support requires at least Rust @0@ on this platform, found @1@'.format(rust_req, rustc.version()))
  endif
  subdir_done()
endif

cdata.set('HAVE_PTP', 1, description : 'PTP support available')

ptp_helper_conf_data = configuration_data()
rust_args = []

setcap_prog = find_program('setcap', '/usr/sbin/setcap', '/sbin/setcap', required : false)
cap_dep = dependency('libcap', required: false)

# user/group to change to in gst-ptp-helper
ptp_helper_setuid_user = get_option('ptp-helper-setuid-user')
if ptp_helper_setuid_user != ''
  ptp_helper_conf_data.set('PTP_HELPER_SETUID_USER', 'Some("@0@")'.format(ptp_helper_setuid_user))
else
  ptp_helper_conf_data.set('PTP_HELPER_SETUID_USER', 'None')
endif
ptp_helper_setuid_group = get_option('ptp-helper-setuid-group')
if ptp_helper_setuid_group != ''
  ptp_helper_conf_data.set('PTP_HELPER_SETUID_GROUP', 'Some("@0@")'.format(ptp_helper_setuid_group))
else
  ptp_helper_conf_data.set('PTP_HELPER_SETUID_GROUP', 'None')
endif

# how to install gst-ptp-helper
with_ptp_helper_permissions = get_option('ptp-helper-permissions')
if with_ptp_helper_permissions == 'auto'
  if setcap_prog.found() and cap_dep.found()
    with_ptp_helper_permissions = 'capabilities'
  elif host_system == 'windows'
    with_ptp_helper_permissions = 'none'
  else
    with_ptp_helper_permissions = 'setuid-root'
  endif
endif
message('How to install gst-ptp-helper: ' + with_ptp_helper_permissions)

if with_ptp_helper_permissions == 'none'
  rust_args += ['--cfg', 'ptp_helper_permissions="none"']
  # nothing to do
elif with_ptp_helper_permissions == 'setuid-root'
  rust_args += ['--cfg', 'ptp_helper_permissions="setuid-root"']
elif with_ptp_helper_permissions == 'capabilities'
  if not setcap_prog.found()
    error('capabilities-based ptp-helper-permissions requested, but could not find setcap tool.')
  elif not cap_dep.found()
    error('capabilities-based ptp-helper-permissions requested, but could not find libcap.')
  endif
  rust_args += ['--cfg', 'ptp_helper_permissions="setcap"']
else
  error('Unexpected ptp helper permissions value: ' + with_ptp_helper_permissions)
endif

conf_lib_rs = configure_file(input : 'conf_lib.rs.in',
                             output : 'conf_lib.rs',
                             configuration: ptp_helper_conf_data)

conf = static_library('gst_ptp_helper_conf', conf_lib_rs,
  override_options : ['rust_std=2018'],
  rust_crate_type : 'rlib')

exe = executable('gst-ptp-helper', 'main.rs',
  override_options : ['rust_std=2018'],
  rust_args : ['-Cpanic=abort', rust_args],
  dependencies : [cap_dep],
  link_with : conf,
  install_dir : helpers_install_dir,
  install : true)

exe_test = executable('gst-ptp-helper-test', 'main.rs',
  override_options : ['rust_std=2018'],
  rust_args : ['--test', rust_args],
  dependencies : [cap_dep],
  link_with : conf,
  install_dir : helpers_install_dir,
  install : true)

test('gst-ptp-helper-test', exe_test, protocol : 'rust')

if host_system != 'windows'
  meson.add_install_script('ptp_helper_post_install.sh',
      helpers_install_dir, with_ptp_helper_permissions,
      setcap_prog.found() ? setcap_prog.full_path() : '')
endif

meson.add_devenv({'GST_PTP_HELPER': exe.full_path()})
