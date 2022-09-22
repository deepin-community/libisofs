AC_INIT([libisofs], [1.5.4], [http://libburnia-project.org])
AC_PREREQ([2.50])
dnl AC_CONFIG_HEADER([config.h])	

AC_CANONICAL_HOST
AC_CANONICAL_TARGET

LIBBURNIA_SET_FLAGS

AM_INIT_AUTOMAKE([subdir-objects])
AC_CONFIG_MACRO_DIR([./])

dnl
dnl if MAJOR or MINOR version changes, be sure to change AC_INIT above to match
dnl
dnl CURRENT and AGE describe the binary compatibility interval of a
dnl dynamic library.
dnl See also http://www.gnu.org/software/libtool/manual.html#Interfaces
dnl
dnl The name of the library will be libisofs.so.$CURRENT-$AGE.$AGE.$REV
dnl In the terminology of this file:
dnl   CURRENT = LT_CURRENT
dnl   REV     = LT_REVISION
dnl   AGE     = LT_AGE
dnl
dnl LT_CURRENT, LT_REVISION and LT_AGE get set directly now.
dnl
dnl SONAME of the emerging library is  LT_CURRENT - LT_AGE.
dnl The linker will do no finer checks. If SONAME matches, then the couple
dnl starts.
dnl 
dnl Therefore a run time check is provided by libisofs function 
dnl iso_lib_version(). It returns the major, minor and micro revision of the 
dnl library. This means LIBISOFS_*_VERSION kept its second job which does not  
dnl comply to the usual ways of configure.ac . I.e. now *officially* this is 
dnl the source code release version as announced to the public. It has no 
dnl connection to SONAME or libtool version numbering.
dnl It rather feeds the API function iso_lib_version().
dnl
dnl If LIBISOFS_*_VERSION changes, be sure to change AC_INIT above to match.
dnl
LIBISOFS_MAJOR_VERSION=1
LIBISOFS_MINOR_VERSION=5
LIBISOFS_MICRO_VERSION=4
LIBISOFS_VERSION=$LIBISOFS_MAJOR_VERSION.$LIBISOFS_MINOR_VERSION.$LIBISOFS_MICRO_VERSION

AC_SUBST(LIBISOFS_MAJOR_VERSION)
AC_SUBST(LIBISOFS_MINOR_VERSION)
AC_SUBST(LIBISOFS_MICRO_VERSION)
AC_SUBST(LIBISOFS_VERSION)

dnl Libtool versioning
LT_RELEASE=$LIBISOFS_MAJOR_VERSION.$LIBISOFS_MINOR_VERSION
dnl 2021.01.30 development jump has not yet happened
dnl SONAME = 97 - 91 = 6 . Library name = libisofs.6.91.0
LT_CURRENT=97
LT_AGE=91
LT_REVISION=0
LT_CURRENT_MINUS_AGE=`expr $LT_CURRENT - $LT_AGE`

AC_SUBST(LT_RELEASE)
AC_SUBST(LT_CURRENT)
AC_SUBST(LT_REVISION)
AC_SUBST(LT_AGE)
AC_SUBST(LT_CURRENT_MINUS_AGE)

AC_PREFIX_DEFAULT([/usr/local])
test "$prefix" = "NONE" && prefix=$ac_default_prefix

dnl ts B90405 : Disabled on advise of Ross Burton
dnl AM_MAINTAINER_MODE

AM_PROG_CC_C_O
AC_C_CONST
AC_C_INLINE
AC_C_BIGENDIAN

dnl Large file support
AC_SYS_LARGEFILE
AC_FUNC_FSEEKO
AC_CHECK_FUNC([fseeko])
if test ! $ac_cv_func_fseeko; then
   AC_MSG_ERROR([Libisofs requires largefile support.])
fi

dnl If iconv(3) is in an extra lib, then it gets added to variable LIBS.
dnl If not, then no -liconv will be added.
LIBBURNIA_CHECK_ICONV

dnl To abort configuration if iconv() still cannot be compiled
LIBBURNIA_ASSERT_ICONV


AC_PROG_LIBTOOL
AC_SUBST(LIBTOOL_DEPS)
# LIBTOOL="$LIBTOOL --silent"

AC_PROG_INSTALL

AC_CHECK_HEADERS()

dnl Use GNU extensions if available
AC_DEFINE(_GNU_SOURCE, 1)

dnl Check for tm_gmtoff field in struct tm
AC_CHECK_MEMBER([struct tm.tm_gmtoff],
	[AC_DEFINE(HAVE_TM_GMTOFF, 1,
		[Define this if tm structure includes a tm_gmtoff entry.])],
	,
	[#include <time.h>])

dnl Check if non standard timegm() function is available
AC_CHECK_DECL([timegm], 
	[AC_DEFINE(HAVE_TIMEGM, 1, [Define this if timegm function is available])],
	,
	[#include <time.h>])

dnl Whether timezone is an integer variable
AH_TEMPLATE([Libburnia_timezonE], [Either timezone or 0])
LIBBURNIA_TRY_TIMEZONE
if test x$LIBBURNIA_TIMEZONE = xtimezone
then
   AC_DEFINE([Libburnia_timezonE], [timezone])
else
   AC_DEFINE([Libburnia_timezonE], [0])
fi

dnl Check if non standard eaccess() function is available
AC_CHECK_DECL([eaccess], 
	[AC_DEFINE(HAVE_EACCESS, 1, [Define this if eaccess function is available])],
	,
	[#include <unistd.h>])

THREAD_LIBS=-lpthread
AC_SUBST(THREAD_LIBS)

TARGET_SHIZZLE
AC_SUBST(ARCH)
AC_SUBST(LIBBURN_ARCH_LIBS)


dnl See if the user wants aggressive optimizations of the code
AC_ARG_ENABLE(debug,
[  --enable-debug          Disable aggressive optimizations, default=yes],
              , enable_debug=yes)
if test x$enable_debug != xyes; then
    if test x$GCC = xyes; then
        CFLAGS="-O3 $CFLAGS"
        CFLAGS="-fexpensive-optimizations $CFLAGS"
    fi
    CFLAGS="-DNDEBUG $CFLAGS"
else
    if test x$GCC = xyes; then
        CFLAGS="-g -pedantic -Wall -Wextra -Wno-unused-parameter -Wno-char-subscripts $CFLAGS"
    fi
    CFLAGS="-DDEBUG $CFLAGS"
fi

dnl Verbose debug to make libisofs issue more debug messages
AC_ARG_ENABLE(verbose-debug,
[  --enable-verbose-debug  Enable verbose debug messages, default=no],
	AC_DEFINE(LIBISOFS_VERBOSE_DEBUG, 1))


dnl Determine target directory for libisofs-*.pc
dnl Important: Must be performed _after_ TARGET_SHIZZLE
dnl
LIBBURNIA_SET_PKGCONFIG


dnl Add compiler-specific flags

AC_ARG_ENABLE(libacl,
[  --enable-libacl         Enable use of ACL functions by libisofs, default=yes],
               , enable_libacl=yes)
LIBACL_DEF=
has_acl_h_but_no_func=0
if test x$LIBBURNIA_SUPP_ACL = xlibacl
then
    if test x$enable_libacl = xyes; then
dnl Check whether there is libacl-devel and libacl-runtime.
dnl If not, erase this macro which would enable use of acl_to_text and others
        LIBACL_DEF="-DLibisofs_with_aaip_acL"
dnl The empty yes case obviously causes -lacl to be linked
        AC_CHECK_HEADER(sys/acl.h, AC_CHECK_LIB(acl, acl_to_text, , has_acl_h_but_no_libacl=1 ), LIBACL_DEF= )
        if test "$has_acl_h_but_no_libacl" = 1
        then
            AC_CHECK_LIB(c, acl_to_text, X= , LIBACL_DEF= )
        fi
    fi
fi
if test x$LIBACL_DEF = x-DLibisofs_with_aaip_acL
then
    if test x$has_acl_h_but_no_libacl = x1
    then
        echo "enabled  local processing of ACL"
    else
        echo "enabled  libacl, local processing of ACL"
    fi
else
    echo "disabled local processing of ACL"
fi
AC_SUBST(LIBACL_DEF)


dnl ts A90123 - B80508
AC_ARG_ENABLE(xattr,
              [  --enable-xattr          Enable use of extended file attributes by libisofs, default=yes],
              , enable_xattr=yes)
AC_ARG_ENABLE(xattr_h_pref_attr,
              [  --enable-xattr-h-pref-attr   Prefer include file attr/xattr.h over sys/xattr.h, default=no],
              , enable_xattr_h_pref_attr=no)

XATTR_DEF=
XATTR_ADDON_DEF=
if test x"$LIBBURNIA_SUPP_FATTR" = xxattr
then
    if test "x$enable_xattr" = xyes; then
dnl Check whether there is the header for Linux xattr.
dnl If not, erase this macro which would enable use of listxattr and others

        XATTR_A_DEF=
        XATTR_S_DEF=
        if test x"$enable_xattr_h_pref_attr" = xyes
        then
            echo "prefering include file attr/xattr.h over sys/attr.h"
            XATTR_A_DEF=1
            AC_CHECK_HEADER(attr/xattr.h, AC_CHECK_LIB(c, listxattr, X= ,
                                                 XATTR_A_DEF= ), XATTR_A_DEF= )
            if test x"$XATTR_A_DEF" = x1
            then
                XATTR_DEF="-DLibisofs_with_aaip_xattR"
            else
                XATTR_S_DEF=1
                AC_CHECK_HEADER(sys/xattr.h, AC_CHECK_LIB(c, listxattr, X= ,
                                                 XATTR_S_DEF= ), XATTR_S_DEF= )
                if test x"$XATTR_S_DEF" = x1
                then
                    XATTR_DEF="-DLibisofs_with_aaip_xattR"
                    XATTR_ADDON_DEF="-DLibisofs_with_sys_xattR"
                fi
            fi
        else
            XATTR_S_DEF=1
            AC_CHECK_HEADER(sys/xattr.h, AC_CHECK_LIB(c, listxattr, X= ,
                                                 XATTR_S_DEF= ), XATTR_S_DEF= )
            if test x"$XATTR_S_DEF" = x1
            then
                XATTR_DEF="-DLibisofs_with_aaip_xattR"
                XATTR_ADDON_DEF="-DLibisofs_with_sys_xattR"
            else
                XATTR_A_DEF=1
                AC_CHECK_HEADER(attr/xattr.h, AC_CHECK_LIB(c, listxattr, X= ,
                                                 XATTR_A_DEF= ), XATTR_A_DEF= )
                if test x"$XATTR_A_DEF" = x1
                then
                    XATTR_DEF="-DLibisofs_with_aaip_xattR"
                fi
            fi
        fi
        if test x"$XATTR_S_DEF" = x1
        then
            echo "decided  to include file sys/attr.h"
        elif test x"$XATTR_A_DEF" = x1
        then
            echo "decided  to include file attr/xattr.h"
        fi
    fi
elif test x"$LIBBURNIA_SUPP_FATTR" = xextattr
then
    if test "x$enable_xattr" = xyes; then
        XATTR_DEF="-DLibisofs_with_freebsd_extattR"
        AC_CHECK_HEADER(sys/extattr.h, AC_CHECK_LIB(c, extattr_list_file, X=,
                                                    XATTR_DEF= ), XATTR_DEF= )
    fi
fi
if test x$XATTR_DEF = x-DLibisofs_with_aaip_xattR
then
    echo "enabled  xattr, local processing of extended file attributes Linux style"
elif test x$XATTR_DEF = x-DLibisofs_with_freebsd_extattR
then
    echo "enabled  extattr, local processing of extended file attributes FreeBSD style"
else
    echo "disabled local processing of extended file attributes"
fi
XATTR_DEF="$XATTR_DEF $XATTR_ADDON_DEF"

AC_SUBST(XATTR_DEF)


dnl ts A90409
AC_ARG_ENABLE(zlib,
[  --enable-zlib           Enable use of zlib by libisofs, default=yes],
               , enable_zlib=yes)
if test "x$enable_zlib" = xyes; then
dnl Check whether there is the header for zlib. 
dnl If not, erase this macro which would enable use of compress2() and others.
dnl Linking fails on SuSE 9.0 because zlib has compress2() but lacks  
dnl compressBound(). So compressBound is the more modern thing to test.
dnl The empty parameter after "compressBound" causes -lz.
    ZLIB_DEF="-DLibisofs_with_zliB"
    AC_CHECK_HEADER(zlib.h, AC_CHECK_LIB(z, compressBound, , ZLIB_DEF= ), ZLIB_DEF= )
else
    ZLIB_DEF=
fi
AC_SUBST(ZLIB_DEF)

dnl ts B00927
AC_ARG_ENABLE(libjte,
[  --enable-libjte           Enable use of libjte >= 2.0 by libisofs, default=yes],
               , enable_libjte=yes)
if test "x$enable_libjte" = xyes; then
    LIBJTE_DEF="-DLibisofs_with_libjtE"
    AC_CHECK_HEADER(libjte/libjte.h, AC_CHECK_LIB(jte, libjte_set_checksum_algorithm, , LIBJTE_DEF= ), LIBJTE_DEF= )
else
    LIBJTE_DEF=
fi
AC_SUBST(LIBJTE_DEF)

# Library versioning normally serves a complex purpose.
# Since libisofs obeys strict ABI backward compatibility, it needs only the
# simple feature to declare function names "global:" or "local:". Only the
# global ones are visible to applications at library load time.
AC_ARG_ENABLE(versioned-libs,
[  --enable-versioned-libs Enable strict symbol encapsulation , default=yes],
               , enable_versioned_libs=yes)
if test x$enable_versioned_libs = xyes; then
    vers_libs_test=no
    LIBISOFS_ASSERT_VERS_LIBS
    if test x$vers_libs_test = xno
    then 
        echo "disabled strict symbol encapsulation (test failed)"
    else
        echo "enabled  strict symbol encapsulation"
    fi
else
    echo "disabled strict symbol encapsulation"
fi

AC_ARG_ENABLE(ldconfig-at-install,
[  --enable-ldconfig-at-install    On GNU/Linux run ldconfig, default=yes],
               , ldconfig_at_install=yes)
if test x$ldconfig_at_install = xyes; then 
    dummy=dummy
else
    LIBBURNIA_LDCONFIG_CMD="echo 'NOTE: ldconfig is disabled. If needed, configure manually for:'"
    echo "disabled run of ldconfig during installation on GNU/Linux"
fi  
AC_SUBST(LIBBURNIA_LDCONFIG_CMD)

AC_CONFIG_FILES([
	Makefile
	doc/doxygen.conf
	version.h
	libisofs-1.pc
	])
AC_OUTPUT
