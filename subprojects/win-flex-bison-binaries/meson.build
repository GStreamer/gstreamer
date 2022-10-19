project('win-flex-bison-binary', version : '2.5.25')

py3 = import('python3').find_python()

message('Downloading and extracting win-flex-bison binaries...')

zip_hash = '8d324b62be33604b2c45ad1dd34ab93d722534448f55a16ca7292de32b6ac135'

ret = run_command(py3, files('download-binary.py'), meson.project_version(), zip_hash, check: true)

meson.override_find_program('flex', find_program('win_flex'))
meson.override_find_program('bison', find_program('win_bison'))
