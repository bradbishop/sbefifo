# Initialize
AC_PREREQ([2.69])
AC_INIT([sbefifo], [1.0], [https://github.com/bradbishop/sbefifo])
#AC_LANG([C++])
AC_CONFIG_HEADERS([config.h])
AM_INIT_AUTOMAKE([subdir-objects -Wall -Werror foreign dist-xz])
AM_SILENT_RULES([yes])

# Check for programs.
AC_PROG_CC
AM_PROG_AR
AC_PROG_INSTALL
AC_PROG_MAKE_SET
AX_PTHREAD

# Check for library functions.
LT_INIT

# Create configured output
AC_CONFIG_FILES([Makefile])
AC_OUTPUT
