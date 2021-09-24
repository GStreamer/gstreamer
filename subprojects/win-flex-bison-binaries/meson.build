project('win-flex-bison-binary', version : '2.5.18')

py3 = import('python3').find_python()

message('Downloading and extracting win-flex-bison binaries...')

zip_hash = '095cf65cb3f12ee5888022f93109acbe6264e5f18f6ffce0bda77feb31b65bd8'

ret = run_command(py3, files('download-binary.py'), meson.project_version(), zip_hash)
if ret.returncode() != 0
  message(ret.stdout())
  error(ret.stderr())
endif

meson.override_find_program('flex', find_program('win_flex'))
meson.override_find_program('bison', find_program('win_bison'))
