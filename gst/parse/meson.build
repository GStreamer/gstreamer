cc = meson.get_compiler('c')

# Find flex, configure lex generator
flex_cdata = configuration_data()

flex = find_program('flex', required : false)
if not flex.found()
  flex = find_program('win_flex', required : false)
  if not flex.found()
    error('flex not found')
  endif
endif

flex_cdata.set('FLEX', flex.path())
if cc.get_id() == 'msvc'
  flex_cdata.set('FLEX_ARGS', '--nounistd')
else
  flex_cdata.set('FLEX_ARGS', '')
endif

gen_lex = configure_file(input : 'gen_lex.py.in',
  output : 'gen_lex.py',
  configuration : flex_cdata)

# Find bison, configure grammar generator
bison_cdata = configuration_data()

bison = find_program('bison', required : false)
if not bison.found()
  bison = find_program('win_bison', required : false)
  if not bison.found()
    error('GNU bison not found')
  endif
endif

bison_cdata.set('BISON', bison.path())
bison_cdata.set('BISON_ARGS', '')

gen_grammar = configure_file(input : 'gen_grammar.py.in',
  output : 'gen_grammar.py',
  configuration : bison_cdata)

# Custom targets
parser = custom_target('parselex',
  input : 'parse.l',
  output : ['lex.priv_gst_parse_yy.c', 'parse_lex.h'],
  command : [py3, gen_lex, '@OUTPUT0@', '@OUTPUT1@', '@INPUT@', 'DUMMY']
)

grammar = custom_target('parsegrammar',
  input : 'grammar.y',
  output : ['grammar.tab.c', 'grammar.tab.h'],
  command : [py3, gen_grammar, '@OUTPUT0@', '@OUTPUT1@', '@INPUT@'],
  depends : [parser],
)
