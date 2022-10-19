project('win-nasm', version : '2.15.04')

py3 = import('python3').find_python()

if host_machine.system() != 'windows'
  error('Can only download nasm for Windows, sorry')
endif

message('Downloading and extracting nasm binaries for Windows...')

arch = host_machine.cpu_family()
if arch == 'x86_64'
  zip_hash = '94204ab5d60324669f71387c2d536cacb37341d27f236358c2ba84bf16c23f6e'
else
  zip_hash = '2b8a00d9fa348e7b68bac0497e25be44db83c64d6681478fdcbfa03fca36ad76'
endif

ret = run_command(py3, files('download-binary.py'), meson.project_version(), arch, zip_hash, check: true)

meson.override_find_program('nasm', find_program(join_paths('nasm-@0@'.format(meson.project_version()), 'nasm')))
