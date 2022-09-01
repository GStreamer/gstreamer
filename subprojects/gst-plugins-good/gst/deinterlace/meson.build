interlace_sources = [
  'gstdeinterlace.c',
  'gstdeinterlacemethod.c',
  'tvtime/tomsmocomp.c',
  'tvtime/greedy.c',
  'tvtime/greedyh.c',
  'tvtime/vfir.c',
  'tvtime/weavetff.c',
  'tvtime/weavebff.c',
  'tvtime/weave.c',
  'tvtime/linear.c',
  'tvtime/linearblend.c',
  'tvtime/scalerbob.c',
  'yadif.c'
]

orcsrc = 'tvtime'
if have_orcc
  orc_h = custom_target(orcsrc + '.h',
    input : orcsrc + '.orc',
    output : orcsrc + '.h',
    command : orcc_args + ['--header', '-o', '@OUTPUT@', '@INPUT@'])
  orc_c = custom_target(orcsrc + '.c',
    input : orcsrc + '.orc',
    output : orcsrc + '.c',
    command : orcc_args + ['--implementation', '-o', '@OUTPUT@', '@INPUT@'])
  orc_targets += {'name': orcsrc, 'orc-source': files(orcsrc + '.orc'), 'header': orc_h, 'source': orc_c}
else
  orc_h = configure_file(input : orcsrc + '-dist.h',
    output : orcsrc + '.h',
    copy : true)
  orc_c = configure_file(input : orcsrc + '-dist.c',
    output : orcsrc + '.c',
    copy : true)
endif

asm_gen_objs = []
if have_nasm and host_cpu == 'x86_64'
  if host_system == 'windows'
    outputname = '@PLAINNAME@.obj'
  else
    outputname = '@PLAINNAME@.o'
  endif

  if get_option('b_staticpic')
    asm_pic_def = '-DPIC'
  else
    asm_pic_def = '-UPIC'
  endif

  # Assembly has to be told when the symbols have to be prefixed with _
  # Note that symbols_have_underscore_prefix does not work properly on macos
  # if the compiler -g flag is used. See:
  # https://github.com/mesonbuild/meson/issues/5482
  if ['darwin', 'ios'].contains(host_system)
    asm_prefix_def = '-DPREFIX'
  elif cc.symbols_have_underscore_prefix()
    asm_prefix_def = '-DPREFIX'
  else
    asm_prefix_def = '-UPREFIX'
  endif

  asm_arch_def = '-DARCH_X86_64=1'
  if host_system == 'windows'
    asm_outformat = 'win64'
  elif ['darwin', 'ios'].contains(host_system)
    asm_outformat = 'macho64'
  else
    asm_outformat = 'elf64'
  endif
  asm_x = files('x86/yadif.asm')

  asm_stackalign_def = '-DSTACK_ALIGNMENT=64'
  asm_incdir = 'x86'

  message('Nasm configured on x86-64')
  asm_gen = generator(nasm,
    output: outputname,
    arguments: ['-I@CURRENT_SOURCE_DIR@',
                '-I@CURRENT_SOURCE_DIR@/@0@/'.format(asm_incdir),
                asm_arch_def,
                asm_stackalign_def,
                asm_pic_def,
                asm_prefix_def,
                '-f', asm_outformat,
                '-o', '@OUTPUT@',
                '@INPUT@'])
  asm_gen_objs = asm_gen.process(asm_x)
endif

gstdeinterlace = library('gstdeinterlace',
  interlace_sources, asm_gen_objs, orc_c, orc_h,
  c_args : gst_plugins_good_args,
  include_directories : [configinc],
  dependencies : [orc_dep, gstbase_dep, gstvideo_dep],
  install : true,
  install_dir : plugins_install_dir,
)
plugins += [gstdeinterlace]

