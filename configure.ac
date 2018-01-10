AC_PREREQ(2.62)

m4_define([cobiwm_major_version], [1])
m4_define([cobiwm_minor_version], [0])
m4_define([cobiwm_micro_version], [0])

m4_define([cobiwm_version],
          [cobiwm_major_version.cobiwm_minor_version.cobiwm_micro_version])

m4_define([cobiwm_plugin_api_version], [1])

AC_INIT([cobiwm], [cobiwm_version],
        [http://bugzilla.gnome.org/enter_bug.cgi?product=cobiwm])

AC_CONFIG_MACRO_DIR([m4])
AC_CONFIG_AUX_DIR([build-aux])
AC_CONFIG_SRCDIR(src/core/display.c)
AC_CONFIG_HEADERS(config.h)

AM_INIT_AUTOMAKE([1.11 foreign no-dist-gzip dist-xz tar-ustar subdir-objects])
m4_ifdef([AM_SILENT_RULES],[AM_SILENT_RULES([yes])],)
AM_MAINTAINER_MODE([enable])

COBIWM_MAJOR_VERSION=cobiwm_major_version
COBIWM_MINOR_VERSION=cobiwm_minor_version
COBIWM_MICRO_VERSION=cobiwm_micro_version
COBIWM_PLUGIN_API_VERSION=cobiwm_plugin_api_version
AC_SUBST(COBIWM_MAJOR_VERSION)
AC_SUBST(COBIWM_MINOR_VERSION)
AC_SUBST(COBIWM_MICRO_VERSION)
AC_SUBST(COBIWM_PLUGIN_API_VERSION)

# Honor aclocal flags
AC_SUBST(ACLOCAL_AMFLAGS, "\${ACLOCAL_FLAGS}")

GETTEXT_PACKAGE=cobiwm
AC_SUBST(GETTEXT_PACKAGE)
AC_DEFINE_UNQUOTED(GETTEXT_PACKAGE,"$GETTEXT_PACKAGE",[Name of default gettext domain])

LT_PREREQ([2.2.6])
LT_INIT([disable-static])
IT_PROG_INTLTOOL([0.41])
AC_PROG_CC
AC_PROG_CC_C_O
AC_PROG_INSTALL
AC_PROG_SED
AC_HEADER_STDC
PKG_PROG_PKG_CONFIG([0.21])

# Sets GLIB_GENMARSHAL and GLIB_MKENUMS
AM_PATH_GLIB_2_0()

CANBERRA_GTK=libcanberra-gtk3
CANBERRA_GTK_VERSION=0.26

CLUTTER_PACKAGE=clutter-1.0

COBIWM_PC_MODULES="
   gtk+-3.0 >= 3.19.8
   gio-unix-2.0 >= 2.35.1
   pango >= 1.2.0
   cairo >= 1.10.0
   gsettings-desktop-schemas >= 3.19.3
   $CLUTTER_PACKAGE >= 1.25.6
   cogl-1.0 >= 1.17.1
   upower-glib >= 0.99.0
   gnome-desktop-3.0
   xcomposite >= 0.2
   xcursor
   xdamage
   xext
   xfixes
   xi >= 1.6.0
   xkbfile
   xkeyboard-config
   xkbcommon >= 0.4.3
   xkbcommon-x11
   xrender
   x11-xcb
   xcb-randr
"

GLIB_GSETTINGS

AC_ARG_ENABLE(verbose-mode,
  AC_HELP_STRING([--disable-verbose-mode],
                 [disable cobiwm's ability to do verbose logging, for embedded/size-sensitive custom builds]),,
  enable_verbose_mode=yes)

if test x$enable_verbose_mode = xyes; then
    AC_DEFINE(WITH_VERBOSE_MODE,1,[Build with verbose mode support])
fi

AC_ARG_ENABLE(sm,
  AC_HELP_STRING([--disable-sm],
                 [disable cobiwm's session management support, for embedded/size-sensitive custom non-GNOME builds]),,
  enable_sm=auto)

AC_ARG_ENABLE(startup-notification,
  AC_HELP_STRING([--disable-startup-notification],
                 [disable cobiwm's startup notification support, for embedded/size-sensitive custom non-GNOME builds]),,
  enable_startup_notification=auto)

AC_ARG_WITH(libcanberra,
  AC_HELP_STRING([--without-libcanberra],
                 [disable the use of libcanberra for playing sounds]),,
  with_libcanberra=auto)

AC_ARG_WITH([xwayland-path],
            [AS_HELP_STRING([--with-xwayland-path], [Absolute path for an X Wayland server])],
            [XWAYLAND_PATH="$withval"],
            [XWAYLAND_PATH="$bindir/Xwayland"])

AC_ARG_ENABLE(installed_tests,
              AS_HELP_STRING([--enable-installed-tests],
                             [Install test programs (default: no)]),,
              [enable_installed_tests=no])
AM_CONDITIONAL(BUILDOPT_INSTALL_TESTS, test x$enable_installed_tests = xyes)

## here we get the flags we'll actually use

# Unconditionally use this dir to avoid a circular dep with gnomecc
GNOME_KEYBINDINGS_KEYSDIR="${datadir}/gnome-control-center/keybindings"
AC_SUBST(GNOME_KEYBINDINGS_KEYSDIR)

STARTUP_NOTIFICATION_VERSION=0.7
AC_MSG_CHECKING([Startup notification library >= $STARTUP_NOTIFICATION_VERSION])
if $PKG_CONFIG --atleast-version $STARTUP_NOTIFICATION_VERSION libstartup-notification-1.0; then
   have_startup_notification=yes
else
   have_startup_notification=no
fi
AC_MSG_RESULT($have_startup_notification)

if test x$enable_startup_notification = xyes; then
   have_startup_notification=yes
   echo "startup-notification support forced on"
elif test x$enable_startup_notification = xauto; then
   true
else
   have_startup_notification=no
fi

if test x$have_startup_notification = xyes; then
  echo "Building with libstartup-notification"
  COBIWM_PC_MODULES="$COBIWM_PC_MODULES libstartup-notification-1.0 >= $STARTUP_NOTIFICATION_VERSION"
  AC_DEFINE(HAVE_STARTUP_NOTIFICATION, , [Building with startup notification support])
else
  echo "Building without libstartup-notification"
fi

have_libcanberra=no
AC_MSG_CHECKING([libcanberra-gtk])
if test x$with_libcanberra = xno ; then
  AC_MSG_RESULT([disabled])
else
  if $PKG_CONFIG --exists $CANBERRA_GTK '>=' $CANBERRA_GTK_VERSION; then
    have_libcanberra=yes
    AC_MSG_RESULT(yes)
    COBIWM_PC_MODULES="$COBIWM_PC_MODULES $CANBERRA_GTK"
    AC_DEFINE([HAVE_LIBCANBERRA], 1, [Building with libcanberra for playing sounds])
  else
    AC_MSG_RESULT(no)
    if test x$with_libcanberra = xyes ; then
      AC_MSG_ERROR([libcanberra forced and libcanberra-gtk was not found])
    fi
  fi
fi

INTROSPECTION_VERSION=0.9.5
GOBJECT_INTROSPECTION_CHECK([$INTROSPECTION_VERSION])

if test x$found_introspection != xno; then
  AC_DEFINE(HAVE_INTROSPECTION, 1, [Define if GObject introspection is available])
  COBIWM_PC_MODULES="$COBIWM_PC_MODULES gobject-introspection-1.0"
  # Since we don't make any guarantees about stability and we don't support
  # parallel install, there's no real reason to change directories, filenames,
  # etc. as we change the Cobiwm tarball version. Note that this must match
  # api_version in src/Makefile.am
  COBIWM_GIR=[Cobiwm_]cobiwm_major_version[_]cobiwm_minor_version[_gir]
  AC_SUBST(COBIWM_GIR)
fi

AC_SUBST(XWAYLAND_PATH)

PKG_CHECK_MODULES(COBIWM, $COBIWM_PC_MODULES)

COBIWM_NATIVE_BACKEND_MODULES="clutter-egl-1.0 libdrm libsystemd libinput gudev-1.0 gbm >= 10.3"

AC_ARG_ENABLE(native-backend,
  AS_HELP_STRING([--disable-native-backend], [disable cobiwm native (KMS) backend]),,
  enable_native_backend=auto
)
AS_IF([test "$enable_native_backend" = "yes"], [have_native_backend=yes],
      [test "$enable_native_backend" = "auto"], PKG_CHECK_EXISTS([$COBIWM_NATIVE_BACKEND_MODULES], [have_native_backend=yes]))

AS_IF([test "$have_native_backend" = "yes"], [
  PKG_CHECK_MODULES([COBIWM_NATIVE_BACKEND], [$COBIWM_NATIVE_BACKEND_MODULES])
  AC_DEFINE([HAVE_NATIVE_BACKEND],[1], [Define if you want to enable the native (KMS) backend based on systemd])
])
AM_CONDITIONAL([HAVE_NATIVE_BACKEND],[test "$have_native_backend" = "yes"])

COBIWM_WAYLAND_MODULES="clutter-wayland-1.0 clutter-wayland-compositor-1.0 wayland-server >= 1.6.90"

AC_ARG_ENABLE(wayland,
  AS_HELP_STRING([--disable-wayland], [disable cobiwm on wayland support]),,
  enable_wayland=auto
)
AS_IF([test "$enable_wayland" = "yes"], [have_wayland=yes],
      [test "$enable_wayland" = "auto"], PKG_CHECK_EXISTS([$COBIWM_WAYLAND_MODULES], [have_wayland=yes]))

AS_IF([test "$have_wayland" = "yes"], [
  PKG_CHECK_MODULES([COBIWM_WAYLAND], [$COBIWM_WAYLAND_MODULES])
  AC_PATH_PROG([WAYLAND_SCANNER],[wayland-scanner],[no])
  AS_IF([test $WAYLAND_SCANNER = "no"],
    [AC_MSG_ERROR([Could not find wayland-scanner in your PATH, required for parsing wayland extension protocols])])
  AC_SUBST([WAYLAND_SCANNER])
  AC_DEFINE([HAVE_WAYLAND],[1],[Define if you want to enable Wayland support])

  PKG_CHECK_MODULES(WAYLAND_PROTOCOLS, [wayland-protocols >= 1.1],
		    [ac_wayland_protocols_pkgdatadir=`$PKG_CONFIG --variable=pkgdatadir wayland-protocols`])
  AC_SUBST(WAYLAND_PROTOCOLS_DATADIR, $ac_wayland_protocols_pkgdatadir)
])
AM_CONDITIONAL([HAVE_WAYLAND],[test "$have_wayland" = "yes"])

PKG_CHECK_EXISTS([xi >= 1.6.99.1],
                 AC_DEFINE([HAVE_XI23],[1],[Define if you have support for XInput 2.3 or greater]))

# This is used for plugins
AC_SUBST(CLUTTER_PACKAGE)
PKG_CHECK_MODULES(CLUTTER, $CLUTTER_PACKAGE)

AC_PATH_XTRA

ALL_X_LIBS="$X_LIBS $X_PRE_LIBS -lX11 $X_EXTRA_LIBS"

# Check for Xinerama extension - we only support the "XFree86" style,
# and not the older Solaris-only version; recent Solaris supports the
# XFree86 style.
cobiwm_save_cppflags="$CPPFLAGS"
CPPFLAGS="$CPPFLAGS $X_CFLAGS"

have_xinerama=yes
AC_CHECK_LIB(Xinerama, XineramaQueryExtension,
    [AC_CHECK_HEADER(X11/extensions/Xinerama.h,
		     [X_EXTRA_LIBS="-lXinerama $X_EXTRA_LIBS"
		     if test -z "`echo $ALL_X_LIBS | grep "\-lXext" 2> /dev/null`"; then
			 X_EXTRA_LIBS="-lXext $X_EXTRA_LIBS"
		     fi],
		     have_xinerama=no,
		     [#include <X11/Xlib.h>])],
    have_xinerama=no, -lXext $ALL_X_LIBS)
AC_MSG_CHECKING(for Xinerama support)
AC_MSG_RESULT($have_xinerama)

CPPFLAGS="$cobiwm_save_cppflags"

if test x$have_xinerama = xno; then
   AC_MSG_ERROR([Xinerama extension was not found])
fi

AC_DEFINE_UNQUOTED([XKB_BASE], ["`$PKG_CONFIG --variable xkb_base xkeyboard-config`"],
                               [XKB base dir])

RANDR_LIBS=
found_randr=no
AC_CHECK_LIB(Xrandr, XRRUpdateConfiguration,
               [AC_CHECK_HEADER(X11/extensions/Xrandr.h,
                                RANDR_LIBS=-lXrandr found_randr=yes,,
				[#include <X11/Xlib.h>])],
               , -lXext $ALL_X_LIBS)

if test "x$found_randr" = "xyes"; then
   AC_DEFINE(HAVE_RANDR, , [Have the Xrandr extension library])
   PKG_CHECK_EXISTS([xrandr >= 1.5.0],
                 AC_DEFINE([HAVE_XRANDR15],[1],[Define if you have support for XRandR 1.5 or greater]))
fi

COBIWM_LIBS="$COBIWM_LIBS $RANDR_LIBS $X_LIBS $X_PRE_LIBS -lX11 $X_EXTRA_LIBS -lm"

found_sm=no
case "$COBIWM_LIBS" in
 *-lSM*)
    found_sm=yes
    ;;
 *)
  AC_CHECK_LIB(SM, SmcSaveYourselfDone,
               [AC_CHECK_HEADERS(X11/SM/SMlib.h,
                                COBIWM_LIBS="-lSM -lICE $COBIWM_LIBS" found_sm=yes)],
               , $COBIWM_LIBS)
    ;;
esac

if test x$enable_sm = xno; then
   found_sm=no
fi

if test x$enable_sm = xyes; then
   if test "$found_sm" = "no"; then
      AC_MSG_ERROR([--enable-sm forced and -lSM not found])
      exit 1
   fi
fi

if test "$found_sm" = "yes"; then
   AC_DEFINE(HAVE_SM, , [Building with SM support])
fi

AM_CONDITIONAL(HAVE_SM, test "$found_sm" = "yes")

AC_PATH_PROG(ZENITY, zenity, no)
if test x"$ZENITY" = xno; then
  AC_MSG_ERROR([zenity not found in your path - needed for dialogs])
fi

AC_ARG_ENABLE(debug,
	[  --enable-debug		enable debugging],,
	enable_debug=no)
if test "x$enable_debug" = "xyes"; then
	CFLAGS="$CFLAGS -g -O"
fi

AC_CHECK_DECL([GL_EXT_x11_sync_object],
              [],
              [AC_MSG_ERROR([GL_EXT_x11_sync_object definition not found, please update your GL headers])],
              [#include <GL/glx.h>])

#### Warnings (last since -Werror can disturb other tests)

# Stay command-line compatible with the gnome-common configure option. Here
# minimum/yes/maximum are the same, however.
AC_ARG_ENABLE(compile_warnings,
  AS_HELP_STRING([--enable-compile-warnings=@<:@no/minimum/yes/maximum/error@:>@],[Turn on compiler warnings]),,
  enable_compile_warnings=error)

changequote(,)dnl
if test "$enable_compile_warnings" != no ; then
  if test "x$GCC" = "xyes"; then
    case " $CFLAGS " in
    *[\ \	]-Wall[\ \	]*) ;;
    *) CFLAGS="$CFLAGS -Wall" ;;
    esac

#   case " $CFLAGS " in
#   *[\ \	]-Wshadow[\ \	]*) ;;
#   *) CFLAGS="$CFLAGS -Wshadow" ;;
#   esac

    case " $CFLAGS " in
    *[\ \	]-Wchar-subscripts[\ \	]*) ;;
    *) CFLAGS="$CFLAGS -Wchar-subscripts" ;;
    esac

    case " $CFLAGS " in
    *[\ \	]-Wmissing-declarations[\ \	]*) ;;
    *) CFLAGS="$CFLAGS -Wmissing-declarations" ;;
    esac

    case " $CFLAGS " in
    *[\ \	]-Wmissing-prototypes[\ \	]*) ;;
    *) CFLAGS="$CFLAGS -Wmissing-prototypes" ;;
    esac

    case " $CFLAGS " in
    *[\ \	]-Wnested-externs[\ \	]*) ;;
    *) CFLAGS="$CFLAGS -Wnested-externs" ;;
    esac

    case " $CFLAGS " in
    *[\ \	]-Wpointer-arith[\ \	]*) ;;
    *) CFLAGS="$CFLAGS -Wpointer-arith" ;;
    esac

    case " $CFLAGS " in
    *[\ \	]-Wcast-align[\ \	]*) ;;
    *) CFLAGS="$CFLAGS -Wcast-align" ;;
    esac

    case " $CFLAGS " in
    *[\ \	]-Wsign-compare[\ \	]*) ;;
    *) CFLAGS="$CFLAGS -Wsign-compare" ;;
    esac

    if test "$enable_compile_warnings" = error; then
      case " $CFLAGS " in
      *[\ \	]-Werror[\ \	]*) ;;
      *) CFLAGS="$CFLAGS -Werror -Wno-error=deprecated-declarations" ;;
      esac
    fi
  fi
fi
changequote([,])dnl

AC_CONFIG_FILES([
Makefile
data/Makefile
doc/Makefile
doc/man/Makefile
src/Makefile
src/libcobiwm.pc
src/include/cobiwm-version.h
po/Makefile.in
])

AC_OUTPUT

if test x$enable_verbose_mode = xno; then
        echo "*** WARNING WARNING WARNING WARNING WARNING"
        echo "*** Building without verbose mode"
        echo "*** This means there's no way to debug cobiwm problems."
        echo "*** Please build normal desktop versions of cobiwm"
        echo "*** with verbose mode enabled so users can use it when they report bugs."
fi

dnl ==========================================================================
echo "
cobiwm-$VERSION

	prefix:                   ${prefix}
	source code location:	  ${srcdir}
	compiler:		  ${CC}

	Startup notification:     ${have_startup_notification}
	libcanberra:              ${have_libcanberra}
	Introspection:            ${found_introspection}
	Session management:       ${found_sm}
	Wayland:                  ${have_wayland}
	Native (KMS) backend:     ${have_native_backend}
"
