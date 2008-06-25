dnl === configure.ac --------------------------------------------------------===
dnl                     The vmkit project
dnl
dnl This file is distributed under the University of Illinois Open Source
dnl License. See LICENSE.TXT for details.
dnl
dnl===-----------------------------------------------------------------------===
dnl This is the VMKit configuration script. It is processed by the autoconf
dnl program to produce a script named configure. This script contains the
dnl configuration checks that VMKit needs in order to support multiple platforms.
dnl This file is composed of 10 sections per the recommended organization of
dnl autoconf input defined in the autoconf documentation. As this file evolves,
dnl please keep the various types of checks within their sections. The sections
dnl are as follows:
dnl
dnl SECTION 1: Initialization & Setup
dnl SECTION 2: Architecture, target, and host checks
dnl SECTION 3: Command line arguments for the configure script.
dnl SECTION 4: Check for programs we need and that they are the right version
dnl SECTION 5: Check for libraries
dnl SECTION 6: Check for header files
dnl SECTION 7: Check for types and structures
dnl SECTION 8: Check for specific functions needed
dnl SECTION 9: Additional checks, variables, etc.
dnl SECTION 10: Specify the output files and generate it
dnl
dnl===-----------------------------------------------------------------------===
dnl===
dnl=== SECTION 1: Initialization & Setup
dnl===
dnl===-----------------------------------------------------------------------===
dnl Initialize autoconf and define the package name, version number and
dnl email address for reporting bugs.
AC_INIT([vmkit],[0.0.1],[nicolas.geoffray@lip6.fr])

dnl Provide a copyright substitution and ensure the copyright notice is included
dnl in the output of --version option of the generated configure script.
AC_SUBST(VMKIT_COPYRIGHT,["Copyright (c) 2003-2008 Universite Pierre et Marie Curie."])
AC_COPYRIGHT([Copyright (c) 2003-2008 Universite Pierre et Marie Curie.])

dnl Indicate that we require autoconf 2.59 or later. Ths is needed because we
dnl use some autoconf macros only available in 2.59.
AC_PREREQ(2.59)

dnl Verify that the source directory is valid. This makes sure that we are
dnl configuring VMKit and not some other package (it validates --srcdir argument)
AC_CONFIG_SRCDIR([lib/Mvm/Object.cpp])


dnl Quit if the source directory has already been configured.
dnl NOTE: This relies upon undocumented autoconf behavior.
if test ${srcdir} != "." ; then
  if test -f ${srcdir}/include/mvm/Config/config.h ; then
    AC_MSG_ERROR([Already configured in ${srcdir}])
  fi
fi

dnl Place all of the extra autoconf files into the config subdirectory. Tell
dnl various tools where the m4 autoconf macros are.
AC_CONFIG_AUX_DIR([$LLVM_SRC_ROOT/autoconf])

dnl Tell autoconf that this is an LLVM project being configured
dnl This provides the --with-llvmsrc and --with-llvmobj options
LLVM_CONFIG_PROJECT($LLVM_SRC_ROOT,$LLVM_OBJ_ROOT)


dnl===-----------------------------------------------------------------------===
dnl===
dnl=== SECTION 2: Architecture, target, and host checks
dnl===
dnl===-----------------------------------------------------------------------===

dnl Check the target for which we're compiling and the host that will do the
dnl compilations. This will tell us which LLVM compiler will be used for
dnl compiling SSA into object code. This needs to be done early because
dnl following tests depend on it.
AC_CANONICAL_TARGET

dnl Determine the platform type and cache its value. This helps us configure
dnl the System library to the correct build platform.
AC_CACHE_CHECK([type of operating system we're going to host on],
               [vmkit_cv_os_type],
[case $host in
  *-*-aix*)
    AC_MSG_ERROR([Good luck porting vmkit to your host!])
    vmkit_cv_os_type="AIX"
    vmkit_cv_platform_type="Unix" ;;
  *-*-irix*)
    AC_MSG_ERROR([Good luck porting vmkit to your host!])
    vmkit_cv_os_type="IRIX"
    vmkit_cv_platform_type="Unix" ;;
  *-*-cygwin*)
    AC_MSG_ERROR([Good luck porting vmkit to your host!])
    vmkit_cv_os_type="Cygwin"
    vmkit_cv_platform_type="Unix" ;;
  *-*-darwin*)
    DYLIB_EXTENSION="dylib"
    vmkit_cv_os_type="Darwin"
    vmkit_cv_platform_type="Unix" ;;
  *-*-freebsd*)
    AC_MSG_ERROR([Good luck porting vmkit to your host!])
    vmkit_cv_os_type="FreeBSD"
    vmkit_cv_platform_type="Unix" ;;
  *-*-openbsd*)
    AC_MSG_ERROR([Good luck porting vmkit to your host!])
    vmkit_cv_os_type="OpenBSD"
    vmkit_cv_platform_type="Unix" ;;
  *-*-netbsd*)
    AC_MSG_ERROR([Good luck porting vmkit to your host!])
    vmkit_cv_os_type="NetBSD"
    vmkit_cv_platform_type="Unix" ;;
  *-*-hpux*)
    AC_MSG_ERROR([Good luck porting vmkit to your host!])
    vmkit_cv_os_type="HP-UX"
    vmkit_cv_platform_type="Unix" ;;
  *-*-interix*)
    AC_MSG_ERROR([Good luck porting vmkit to your host!])
    vmkit_cv_os_type="Interix"
    vmkit_cv_platform_type="Unix" ;;
  *-*-linux*)
    DYLIB_EXTENSION="so"
    vmkit_cv_os_type="Linux"
    vmkit_cv_platform_type="Unix" ;;
  *-*-solaris*)
    AC_MSG_ERROR([Good luck porting vmkit to your host!])
    vmkit_cv_os_type="SunOS"
    vmkit_cv_platform_type="Unix" ;;
  *-*-win32*)
    AC_MSG_ERROR([Good luck porting vmkit to your host!])
    vmkit_cv_os_type="Win32"
    vmkit_cv_platform_type="Win32" ;;
  *-*-mingw*)
    AC_MSG_ERROR([Good luck porting vmkit to your host!])
    vmkit_cv_os_type="MingW"
    vmkit_cv_platform_type="Win32" ;;
  *)
    AC_MSG_ERROR([Good luck porting vmkit to your host!])
    vmkit_cv_os_type="Unknown"
    vmkit_cv_platform_type="Unknown" ;;
esac])

dnl Make sure we aren't attempting to configure for an unknown system

AC_SUBST([DYLIB_EXTENSION])

dnl===-----------------------------------------------------------------------===
dnl===
dnl=== SECTION 3: Command line arguments for the configure script.
dnl===
dnl===-----------------------------------------------------------------------===

AC_ARG_WITH(thread,
       [AS_HELP_STRING(--with-thread=something,
           [Thread type ('common' or 'no')])],
       [thread=$withval],[thread=common]
)

AS_IF([test "x$thread" != "xno"],
  [AC_CHECK_HEADER([pthread.h],,
    [AC_MSG_WARN(phtread include NOT found)])
  AC_CHECK_LIB(pthread, pthread_create, [], 
     [AC_MSG_ERROR([pthread library not found])])
  ]
)

if test "x$thread" = xcommon; then
  AC_DEFINE([HAVE_PTHREAD], [1], [Using pthread library])
fi

dnl **************************************************************************
dnl VVM GC type 
dnl **************************************************************************
AC_ARG_WITH(gc,
       [AS_HELP_STRING(--with-gc=something,
           [GC type ('single-mmap' 'multi-mmap' or 'boehm')])],
       [[gc=$withval]],
       [[ echo Using mmap2 as vvm gc type.
                gc=single-mmap
       ]]
)

if test "x$gc" = "xboehm";  then
  GC_FLAGS ="-I$PWD/lib/Mvm/BoehmGC -DGC_THREADS"
  AC_DEFINE([USE_GC_BOEHM], [1], [Using the boehm gc])
  AC_SUBST(GC_MMAP2, [0])
  AC_SUBST(GC_BOEHM, [1])
  GC_LIBS=BoehmGC
  case $target_os in
    *linux*)
      GC_FLAGS="-I$PWD/lib/Mvm/BoehmGC -DGC_THREADS -DGC_LINUX_THREADS"
    ;;
  esac
else
  GC_LIBS=GCMmap2
  if test "x$gc" = "xmulti-mmap"; then
    GC_FLAGS="-I$PWD/lib/Mvm/GCMmap2 -DWITH_TRACER -DMULTIPLE_GC"
    AC_SUBST([GC_MULTI_MMAP], [1])
    AC_SUBST([GC_SINGLE_MMAP], [0])
  else
    GC_FLAGS="-I$PWD/lib/Mvm/GCMmap2 -DWITH_TRACER"
    AC_SUBST([GC_MULTI_MMAP], [0])
    AC_SUBST([GC_SINGLE_MMAP], [1])
  fi
  AC_DEFINE([USE_GC_MMAP2], [1], [Using the gcmmap2])
  AC_SUBST(GC_MMAP2, [1])
  AC_SUBST(GC_BOEHM, [0])
fi
  
AC_SUBST([GC_FLAGS])
AC_SUBST([GC_LIBS])

dnl **************************************************************************
dnl Virtual Machine type
dnl **************************************************************************
AC_ARG_WITH(vm-type,
       [AS_HELP_STRING(--with-vm-type=something,
           [VM type ('single' 'multi' or 'service')])],
       [[vmtype=$withval]],
       [[ echo Using single as vm type.
                vmtype=single
       ]]
)

if test "x$vmtype" = "xmulti";  then
  CFLAGS="$CFLAGS -DMULTIPLE_VM"
  CXXFLAGS="$CXXFLAGS -DMULTIPLE_VM"
else 
  if test "x$vmtype" = "xservice"; then
    CFLAGS="$CFLAGS -DMULTIPLE_VM -DSERVICE_GC -DMULTIPLE_GC -DSERVICE_VM -I$PWD/lib/Mvm/Allocator"
    CXXFLAGS="$CXXFLAGS -DMULTIPLE_VM -DSERVICE_GC -DMULTIPLE_GC -DSERVICE_VM -I$PWD/lib/Mvm/Allocator"
  fi
fi

AM_CONDITIONAL([SERVICE_BUILD], [test "x$vmtype" = "xservice"])
AM_CONDITIONAL([ISOLATE_BUILD], [test "x$vmtype" = "xmulti"])
  
dnl **************************************************************************
dnl GNU CLASSPATH installation prefix
dnl **************************************************************************

gnuclasspathversion=0.93;

AC_ARG_WITH(gnu-classpath-local-prefix,
       [AS_HELP_STRING(--with-gnu-classpath-local-prefix=something,
           [GNU CLASSPATH local prefix (no default)])],
       [[gnuclasspathlocalprefix=$withval]],
       [[       echo Not using GNU CLASSPATH local prefix.    
                                gnuclasspathlocalprefix=''
       ]]
)

AC_ARG_WITH(gnu-classpath-installation-prefix,
       [AS_HELP_STRING(--with-gnu-classpath-installation-prefix=something,
           [GNU CLASSPATH installation prefix (default is '/usr/local/classpath')])],
       [[gnuclasspathinstallationprefix=$withval]],
       [[gnuclasspathinstallationprefix=/usr/local/classpath]]
)

if test "x${gnuclasspathlocalprefix}" = x; then
  echo Using ${gnuclasspathinstallationprefix} as GNU CLASSPATH installation prefix;
  classpathglibj=${gnuclasspathinstallationprefix}/share/classpath/glibj.zip;
  classpathlibs=${gnuclasspathinstallationprefix}/lib/classpath/;
  classpathinclude=${gnuclasspathlocalprefix}/include;
else
  echo Using ${gnuclasspathlocalprefix} as GNU CLASSPATH local prefix;
  classpathglibj=${gnuclasspathlocalprefix}/lib/;
  classpathlibs=${gnuclasspathlocalprefix}/lib/;
  classpathinclude=${gnuclasspathlocalprefix}/include;
fi


AC_SUBST([classpathglibj])
AC_SUBST([classpathlibs])
AC_SUBST([classpathinclude])
AC_SUBST([gnuclasspathversion])

dnl **************************************************************************
dnl Local PNet directory
dnl **************************************************************************
AC_ARG_WITH(pnet-local-prefix,
       [AS_HELP_STRING(--with-pnet-local-prefix=something,
           [PNET local prefix (no default)])],
       [[pnetlocalprefix=$withval]],
       [[       echo Not using PNETlocal prefix.    
                                pnetlocalprefix=''
       ]]
)

AM_CONDITIONAL([WITH_N3], [test "x$pnetlocalprefix" != "x"])
if test "x$pnetlocalprefix" != x; then
  echo Using ${pnetlocalprefix} as PNET local prefix;
  AC_DEFINE([WITH_N3], [1], [Compiling N3])
fi

AC_SUBST([pnetlocalprefix])

dnl===-----------------------------------------------------------------------===
dnl===
dnl=== SECTION 4: Check for programs we need and that they are the right version
dnl===
dnl===-----------------------------------------------------------------------===

dnl Check for compilation tools
AC_PROG_CPP
AC_PROG_CC(gcc)
AC_PROG_CXX(g++)

AC_PROG_NM
AC_SUBST(NM)

dnl Check for the tools that the makefiles require
AC_PROG_LN_S
AC_PATH_PROG(CMP, [cmp], [cmp])
AC_PATH_PROG(CP, [cp], [cp])
AC_PATH_PROG(DATE, [date], [date])
AC_PATH_PROG(FIND, [find], [find])
AC_PATH_PROG(GREP, [grep], [grep])
AC_PATH_PROG(MKDIR,[mkdir],[mkdir])
AC_PATH_PROG(MV,   [mv],   [mv])
AC_PROG_RANLIB
AC_PATH_PROG(RM,   [rm],   [rm])
AC_PATH_PROG(SED,  [sed],  [sed])
AC_PATH_PROG(TAR,  [tar],  [gtar])
AC_PATH_PROG(BINPWD,[pwd],  [pwd])
AC_PATH_PROG(CAT,[cat],  [cat])

AC_PATH_PROG(LLVMAS,[llvm-as],  [llvm-as])
AC_PATH_PROG(LLC,[llc],  [llc])

dnl Find the install program
AC_PROG_INSTALL

if test "$WITH_LLVMGCCDIR" = "default" ; then
  LLVMGCC="llvm-gcc${EXEEXT}"
  LLVMGXX="llvm-g++${EXEEXT}"
  AC_PATH_PROG(LLVMGCC, $LLVMGCC, [])
  AC_PATH_PROG(LLVMGXX, $LLVMGXX, [])
else
  if test -z "$LLVMGCC"; then
    LLVMGCC="$WITH_LLVMGCCDIR/bin/llvm-gcc${EXEEXT}"
  fi
  if test -z "$LLVMGXX"; then
    LLVMGXX="$WITH_LLVMGCCDIR/bin/llvm-g++${EXEEXT}"
  fi
  AC_SUBST(LLVMGCC,$LLVMGCC)
  AC_SUBST(LLVMGXX,$LLVMGXX)
fi

AC_MSG_CHECKING([tool compatibility])

dnl Ensure that compilation tools are GCC or a GNU compatible compiler such as
dnl ICC; we use GCC specific options in the makefiles so the compiler needs
dnl to support those options.
dnl "icc" emits gcc signatures
dnl "icc -no-gcc" emits no gcc signature BUT is still compatible
ICC=no
IXX=no
case $CC in
  icc*|icpc*)
    ICC=yes
    IXX=yes
    ;;
   *)
    ;;
esac

if test "$GCC" != "yes" && test "$ICC" != "yes"
then
  AC_MSG_ERROR([gcc|icc required but not found])
fi

dnl Ensure that compilation tools are GCC; we use GCC specific extensions
if test "$GXX" != "yes" && test "$IXX" != "yes"
then
  AC_MSG_ERROR([g++|icc required but not found])
fi

dnl Verify that GCC is version 3.0 or higher
if test "$GCC" = "yes"
then
  AC_COMPILE_IFELSE([[#if !defined(__GNUC__) || __GNUC__ < 3
#error Unsupported GCC version
#endif
]], [], [AC_MSG_ERROR([gcc 3.x required, but you have a lower version])])
fi

dnl Tool compatibility is okay if we make it here.
AC_MSG_RESULT([ok])


dnl===-----------------------------------------------------------------------===
dnl===
dnl=== SECTION 5: Check for libraries
dnl===
dnl===-----------------------------------------------------------------------===

AC_CHECK_LIB(z, inflate, [], \
  [AC_MSG_ERROR([You need to install the zlib package (z).])]
)
  
if test "x$gc" = "xboehm";  then
AC_CHECK_LIB(gc, GC_malloc, [], \
    [AC_MSG_ERROR([You need to install the boehm-gc package (gc).])]
)
fi

dnl===-----------------------------------------------------------------------===
dnl===
dnl=== SECTION 6: Check for header files
dnl===
dnl===-----------------------------------------------------------------------===

AC_CHECK_HEADER([zlib.h], [], \
  AC_MSG_ERROR([You need to install the zlib devel package (zlib.h).])
)
  
if test "x$gc" = "xboehm";  then
  AC_CHECK_HEADER([gc/gc.h], [], \
    AC_MSG_ERROR([You need to install the boehm-gc devel package (gc/gc.h).])
  )
fi


nl===-----------------------------------------------------------------------===
dnl===
dnl=== SECTION 7: Check for types and structures
dnl===
dnl===-----------------------------------------------------------------------===

AC_TYPE_PID_T
AC_TYPE_SIZE_T
AC_TYPE_SIGNAL
AC_STRUCT_TM
AC_CHECK_TYPES([int64_t],,AC_MSG_ERROR([Type int64_t required but not found]))
AC_CHECK_TYPES([uint64_t],,
         AC_CHECK_TYPES([u_int64_t],,
         AC_MSG_ERROR([Type uint64_t or u_int64_t required but not found])))

dnl===-----------------------------------------------------------------------===
dnl===
dnl=== SECTION 8: Check for specific functions needed
dnl===
dnl===-----------------------------------------------------------------------===

AC_CHECK_FUNCS([setjmp longjmp])


dnl===-----------------------------------------------------------------------===
dnl===
dnl=== SECTION 9: Additional checks, variables, etc.
dnl===
dnl===-----------------------------------------------------------------------===

dnl===-----------------------------------------------------------------------===
dnl===
dnl=== SECTION 10: Specify the output files and generate it
dnl===
dnl===-----------------------------------------------------------------------===

dnl Configure header files
dnl WARNING: dnl If you add or remove any of the following config headers, then
dnl you MUST also update Makefile.rules so that the variable FilesToConfig
dnl contains the same list of files as AC_CONFIG_HEADERS below. This ensures the
dnl files can be updated automatically when their *.in sources change.
AC_CONFIG_HEADERS([include/mvm/Config/config.h])

dnl Verify that the source directory is valid
AC_CONFIG_SRCDIR(["Makefile.common.in"])

dnl Configure a common Makefile
AC_CONFIG_FILES(Makefile.common)
AC_CONFIG_FILES(Makefile.config)
AC_CONFIG_FILES([lib/JnJVM/Classpath/Classpath.h])

dnl Do special configuration of Makefiles
AC_CONFIG_MAKEFILE(Makefile)
AC_CONFIG_MAKEFILE(lib/Makefile)


dnl **************************************************************************
dnl LLVM Installation Prefix 
dnl **************************************************************************
dnl LLVMDYLIB="`$llvmprefix/Release/bin/llvm-config --ldflags all` `$llvmprefix/Release/bin/llvm-config --libs all`"
dnl AC_SUBST([LLVMDYLIB])

AC_OUTPUT