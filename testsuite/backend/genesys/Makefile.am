##  Makefile.am -- an automake template for Makefile.in file
##  Copyright (C) 2019  Sane Developers.
##
##  This file is part of the "Sane" build infra-structure.  See
##  included LICENSE file for license information.

TEST_LDADD = \
  ../../../sanei/libsanei.la \
  ../../../lib/liblib.la \
  ../../../backend/libgenesys.la \
  ../../../backend/sane_strstatus.lo \
  $(MATH_LIB) $(USB_LIBS) $(PTHREAD_LIBS)

check_PROGRAMS = genesys_tests
TESTS = $(check_PROGRAMS)

AM_CPPFLAGS += -I. -I$(srcdir) -I$(top_builddir)/include -I$(top_srcdir)/include $(USB_CFLAGS) \
    -DBACKEND_NAME=genesys

genesys_tests_SOURCES = tests.cc tests.h minigtest.cc minigtest.h \
    tests_calibration.cc

genesys_tests_LDADD = $(TEST_LDADD)
