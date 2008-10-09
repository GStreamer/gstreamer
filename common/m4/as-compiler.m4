dnl as-compiler.m4 0.1.0
                                                                                
dnl autostars m4 macro for detection of compiler flavor

dnl Thomas Vander Stichele <thomas at apestaart dot org>

dnl $Id: as-compiler.m4,v 1.4 2004/06/01 09:33:45 thomasvs Exp $
                                                                                
dnl AS_COMPILER(COMPILER)
dnl will set variable COMPILER to
dnl - gcc
dnl - forte
dnl - (empty) if no guess could be made

AC_DEFUN([AS_COMPILER],
[
  as_compiler=
  AC_MSG_CHECKING(for compiler flavour)

  dnl is it gcc ?
  if test "x$GCC" = "xyes"; then
    as_compiler="gcc"
  fi

  dnl is it forte ?
  AC_TRY_RUN([
int main
(int argc, char *argv[])
{
#ifdef __sun
  return 0;
#else
  return 1;
#endif
}
  ], as_compiler="forte", ,)

  if test "x$as_compiler" = "x"; then
    AC_MSG_RESULT([unknown !])
  else
    AC_MSG_RESULT($as_compiler)
  fi
  [$1]=$as_compiler
])
