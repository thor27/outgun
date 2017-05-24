#  Makefile
#
#  Copyright (C) 2003-2008 - Niko Ritari
#
#  This file is part of Outgun.
#
#  Outgun is free software; you can redistribute it and/or modify
#  it under the terms of the GNU General Public License as published by
#  the Free Software Foundation; either version 2 of the License, or
#  (at your option) any later version.
#
#  Outgun is distributed in the hope that it will be useful,
#  but WITHOUT ANY WARRANTY; without even the implied warranty of
#  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#  GNU General Public License for more details.
#
#  You should have received a copy of the GNU General Public License
#  along with Outgun; if not, write to the Free Software
#  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA


# This makefile makes heavy use of features specific to GNU make.
# If you get errors about makefile syntax, make sure you're using the right make tool.
# On some systems GNU make goes by the name gmake instead of make.

# Items that might need user configuration are at the beginning of the Makefile.
# See the end for goal keywords. If none are specified, Outgun client and the main tools are built.


# -- Platform type selection: --

ifndef LINUX
 ifndef WINDOWS

  ifeq "$(MAKE)" "mingw32-make"
   WINDOWS=1
  else
   LINUX=1
   ifndef XAUTHORITY
    $(warning Linux/Unix build assumed. Use "$(MAKE) WINDOWS=1" to override, or "$(MAKE) LINUX=1" to hide this warning.)
   endif
  endif

 endif
endif

# -- User configurable items: --

CFLAGS ?= -W -Wall -O3 -ffast-math -fno-enforce-eh-specs -fpermissive
CXXFLAGS ?= $(CFLAGS)

CC ?= gcc
CXX ?= g++
SVNVERSION ?= svnversion

DEPDIR ?= dep
OBJDIR ?= obj
BINDIR ?= bin
TARGETBINDIR ?= ..
# TARGETBINDIR is for end-user binaries, BINDIR for others.
STATUSDIR ?= makestatus
# The parent directories of each directory must already exist, but the directories themselves are automatically created.

# -- Platform specific items that may need user configuration: --

ifdef LINUX

 RM ?= rm -f
 MKDIR ?= mkdir
 EXE_SUFFIX :=

 ALLEGRO_CONFIG ?= allegro-config
 ifdef DEBUGLIBS
  COMMON_LIBS := -lNL_debug
  ALLEG_LIBS := `$(ALLEGRO_CONFIG) --libs debug`
  ALLEG_CFLAGS := `$(ALLEGRO_CONFIG) --cflags debug`
 else
  COMMON_LIBS := -lNL
  ALLEG_LIBS := `$(ALLEGRO_CONFIG) --libs`
  ALLEG_CFLAGS := `$(ALLEGRO_CONFIG) --cflags`
 endif
 PNG_LIBS := -lz -lpng
 PNG_CFLAGS := -DWITH_PNG
 PLATFORM_OBJ_NAMES := platform_unix.o
 OUTGUN_LDFLAGS := -pthread
 BUILDTOOL_LDFLAGS :=
 MON_LDFLAGS := -pthread
 RELAY_LDFLAGS := -pthread
 TEST_LDFLAGS := -pthread

else

 CFLAGS += -mthreads
 CXXFLAGS += -mthreads

 WINDRES ?= windres
 RM ?= del
 MKDIR ?= mkdir
 EXE_SUFFIX := .exe

 COMMON_LIBS := -lNL -lpthreadGC1 -lwinmm
 PNG_LIBS := -lz -lpng
 PNG_CFLAGS := -DWITH_PNG
 ALLEG_LIBS := -lalleg
 ALLEG_CFLAGS :=
 PLATFORM_OBJ_NAMES := platform_win.o
 OUTGUN_COMMON_OBJ_NAMES += icon.res
 OUTGUN_LDFLAGS := -mwindows -mthreads
 BUILDTOOL_LDFLAGS := -mconsole
 MON_LDFLAGS := -mconsole -mthreads
 RELAY_LDFLAGS := -mconsole -mthreads
 TEST_LDFLAGS := -mconsole -mthreads

 $(OBJDIR)/gui/%.res: %.rc
	$(WINDRES) -i $< -O coff -o $@

endif

# -- End of user configurable items. --


# default goal (defined at the end)
default:

.SUFFIXES:
.DELETE_ON_ERROR:

#ifeq ($(wildcard .svn),.svn)
# SVN_REVISION := $(shell $(SVNVERSION))
#endif
# fixed for exported copy
SVN_REVISION := 336E

# -- Compiler flags: --

BUILDTOOL_LIBS :=
MON_LIBS := $(COMMON_LIBS)
RELAY_LIBS := $(COMMON_LIBS)
TEST_LIBS := $(COMMON_LIBS) $(ALLEG_LIBS)

TEXT_CXXFLAGS := $(CXXFLAGS) -DDEDICATED_SERVER_ONLY

GUI_CXXFLAGS := $(CXXFLAGS) $(ALLEG_CFLAGS)
GUI_CFLAGS := $(CFLAGS) $(ALLEG_CFLAGS)

OUTGUN_DEDSERV_LIBS := $(COMMON_LIBS)
OUTGUN_CLIENT_LIBS := $(COMMON_LIBS) $(ALLEG_LIBS)
ifdef WITH_PNG
 GUI_CXXFLAGS += $(PNG_CFLAGS)
 OUTGUN_CLIENT_LIBS += $(PNG_LIBS)
endif

# -- Source files: --

SRC_PREFIXES := * leetnet/* loadpng/* tests/* tools/*
# Suffix order is important for dependency generation.
# Files that generate Makefile rules (.cpp and .c) must be included last.
SRC_SUFFIXES := .h .inc .cpp .c
SRC_FILE_PATTERNS := $(foreach SUFFIX,$(SRC_SUFFIXES),$(addsuffix $(SUFFIX),$(SRC_PREFIXES)))
GENERATED_HEADER_FILES := generated/svnrevision.inc
SRC_FILES := $(GENERATED_HEADER_FILES) $(filter-out $(SRC_FILE_PATTERNS),$(wildcard $(SRC_FILE_PATTERNS)))

# -- Source file dependency list files: --

DEPENDENCY_FILES := $(patsubst %,$(DEPDIR)/%.d,$(SRC_FILES))

# -- Object files: --

OUTGUN_COMMON_OBJ_NAMES += world.o servnet.o server.o server_settings.o commont.o main.o names.o auth.o nassert.o globals.o log.o utility.o network.o thread.o gamemod.o debug.o robot.o client.o timer.o language.o mapgen.o version.o mutex.o binaryaccess.o $(PLATFORM_OBJ_NAMES)
OUTGUN_CLIENT_OBJ_NAMES := $(OUTGUN_COMMON_OBJ_NAMES) antialias.o graphics.o colour.o client_menus.o sounds.o menu.o mappic.o
ifdef WITH_PNG
 OUTGUN_CLIENT_OBJ_NAMES += loadpng/loadpng.o loadpng/savepng.o loadpng/regpng.o
endif
LEETNET_OBJ_NAMES := rudp.o client.o server.o Time.o Timer.o dlog.o

RELAY_OBJ_NAMES := tools/relay.o binaryaccess.o commont.o debug.o globals.o language.o log.o mutex.o nassert_simple.o network.o timer.o utility.o version.o $(PLATFORM_OBJ_NAMES)
MAKEDEP_OBJ_NAMES := tools/makedep.o
WRITEIFDIFF_OBJ_NAMES := tools/writeifdifferent.o
MON_OBJ_NAMES := tools/srvmonit.o nassert_simple.o network.o utility.o globals.o language.o log.o commont.o timer.o version.o debug.o mutex.o binaryaccess.o $(PLATFORM_OBJ_NAMES)

LEETNET_OBJS := $(patsubst %,$(OBJDIR)/leetnet/%,$(LEETNET_OBJ_NAMES))
OUTGUN_CLIENT_OBJS := $(patsubst %,$(OBJDIR)/gui/%,$(OUTGUN_CLIENT_OBJ_NAMES)) $(LEETNET_OBJS)
OUTGUN_DEDSERV_OBJS := $(patsubst %,$(OBJDIR)/text/%,$(OUTGUN_COMMON_OBJ_NAMES)) $(LEETNET_OBJS)

RELAY_OBJS := $(patsubst %,$(OBJDIR)/text/%,$(RELAY_OBJ_NAMES))
MAKEDEP_OBJS := $(patsubst %,$(OBJDIR)/text/%,$(MAKEDEP_OBJ_NAMES))
WRITEIFDIFF_OBJS := $(patsubst %,$(OBJDIR)/text/%,$(WRITEIFDIFF_OBJ_NAMES))
MON_OBJS := $(patsubst %,$(OBJDIR)/text/%,$(MON_OBJ_NAMES))

OBJECTS := $(OUTGUN_CLIENT_OBJS) $(OUTGUN_DEDSERV_OBJS) $(MAKEDEP_OBJS) $(WRITEIFDIFF_OBJS) $(MON_OBJS) $(RELAY_OBJS)

# -- Target files: --

OUTGUN_EXE := $(TARGETBINDIR)/outgun$(EXE_SUFFIX)
OUTGUN_DED_EXE := $(TARGETBINDIR)/outgun-ded$(EXE_SUFFIX)
SRVMONIT_EXE := $(TARGETBINDIR)/srvmonit$(EXE_SUFFIX)
RELAY_EXE := $(TARGETBINDIR)/relay$(EXE_SUFFIX)
MAKEDEP_EXE := $(BINDIR)/makedep$(EXE_SUFFIX)
WRITEIFDIFF_EXE := $(BINDIR)/writeifdifferent$(EXE_SUFFIX)

TEST_TARGETS := $(patsubst tests/%.cpp,$(BINDIR)/tests/%$(EXE_SUFFIX),$(filter-out tests/tests.cpp,$(wildcard tests/*.cpp)))
TEST_EXEC_TARGETS = $(patsubst $(BINDIR)/tests/%$(EXE_SUFFIX),test_%,$(TEST_TARGETS))
TEST_PASS_MARKERS := $(patsubst %,$(STATUSDIR)/%,$(TEST_EXEC_TARGETS))

TARGETS := $(OUTGUN_EXE) $(OUTGUN_DED_EXE) $(SRVMONIT_EXE) $(RELAY_EXE) $(MAKEDEP_EXE) $(WRITEIFDIFF_EXE) $(TEST_TARGETS)

### Above: definitions. ### Below: actions. ###

# -- Directories for generated files: --

BIN_SUBDIR_NAMES = tests
DEP_SUBDIR_NAMES = generated leetnet loadpng tests tools
OBJ_SUBDIR_NAMES = gui leetnet tests text
OBJ_GUI_SUBDIR_NAMES = loadpng
OBJ_TEXT_SUBDIR_NAMES = tools

BIN_SUBDIRS = $(patsubst %,$(BINDIR)/%,$(BIN_SUBDIR_NAMES))
DEP_SUBDIRS = $(patsubst %,$(DEPDIR)/%,$(DEP_SUBDIR_NAMES))
OBJ_SUBDIRS = $(patsubst %,$(OBJDIR)/%,$(OBJ_SUBDIR_NAMES))
OBJ_GUI_SUBDIRS = $(patsubst %,$(OBJDIR)/gui/%,$(OBJ_GUI_SUBDIR_NAMES))
OBJ_TEXT_SUBDIRS = $(patsubst %,$(OBJDIR)/text/%,$(OBJ_TEXT_SUBDIR_NAMES))

GENERATED_DIRS = generated $(BINDIR) $(BIN_SUBDIRS) $(DEPDIR) $(DEP_SUBDIRS) $(OBJDIR) $(OBJ_SUBDIRS) $(OBJ_GUI_SUBDIRS) $(OBJ_TEXT_SUBDIRS) $(STATUSDIR)

$(GENERATED_DIRS):
	-$(MKDIR) $@

$(BIN_SUBDIRS): | $(BINDIR)
$(DEP_SUBDIRS): | $(DEPDIR)
$(OBJ_SUBDIRS): | $(OBJDIR)
$(OBJ_GUI_SUBDIRS): | $(OBJDIR)/gui
$(OBJ_TEXT_SUBDIRS): | $(OBJDIR)/text

# make-directories-dummy must exist and is not generated anywhere (because its date should stay old).
# It is used to work around a gmake bug.
make-directories-dummy: | $(GENERATED_DIRS)

# -- Generated source files: --

WRITEIFDIFFERENT := $(WRITEIFDIFF_EXE)

generated/svnrevision.inc :: | make-directories-dummy $(WRITEIFDIFFERENT)

generated/svnrevision.inc ::
	@$(WRITEIFDIFFERENT) $@ "#define SVN_REVISION \"$(SVN_REVISION)\""

# -- Source file dependency list files: --

MAKEDEP := $(MAKEDEP_EXE)

$(DEPDIR)/leetnet/%.d: leetnet/% $(MAKEDEP) | make-directories-dummy
	@$(MAKEDEP) -O $(OBJDIR) $< > $@

$(DEPDIR)/loadpng/%.d: loadpng/% $(MAKEDEP) | make-directories-dummy
	@$(MAKEDEP) -O $(OBJDIR)/gui $< > $@

$(DEPDIR)/tests/%.d: tests/% $(MAKEDEP) | make-directories-dummy
	@$(MAKEDEP) -O $(OBJDIR) $< > $@

$(DEPDIR)/tools/%.d: tools/% $(MAKEDEP) | make-directories-dummy
	@$(MAKEDEP) -O $(OBJDIR)/text $< > $@

$(DEPDIR)/%.d: % $(MAKEDEP) | make-directories-dummy
	@$(MAKEDEP) -O $(OBJDIR)/gui -O $(OBJDIR)/text $< > $@

-include $(DEPENDENCY_FILES)

# -- Object files: --

$(OBJDIR)/gui/%.o: %.cpp | make-directories-dummy
	$(CXX) $(CPPFLAGS) $(GUI_CXXFLAGS) -c -o $@ $<

$(OBJDIR)/gui/loadpng/%.o: loadpng/%.c | make-directories-dummy
	$(CC) $(CPPFLAGS) $(GUI_CFLAGS) -c -o $@ $<

$(OBJDIR)/tests/%.o: tests/%.cpp | make-directories-dummy
	$(CXX) $(CPPFLAGS) $(GUI_CXXFLAGS) -c -o $@ $<

$(OBJDIR)/text/%.o: %.cpp | make-directories-dummy
	$(CXX) $(CPPFLAGS) $(TEXT_CXXFLAGS) -c -o $@ $<

$(OBJDIR)/text/nassert_simple.o: $(OBJDIR)/text/nassert.o | make-directories-dummy
	$(CXX) $(CPPFLAGS) $(TEXT_CXXFLAGS) -DDISABLE_ENHANCED_NASSERT -c -o $@ nassert.cpp

$(OBJDIR)/leetnet/%.o: leetnet/%.cpp | make-directories-dummy
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c -o $@ $<

# -- Target binaries: --

$(OUTGUN_EXE): $(OUTGUN_CLIENT_OBJS)
	$(CXX) $(OUTGUN_LDFLAGS) -o $@ $(OUTGUN_CLIENT_OBJS) $(OUTGUN_CLIENT_LIBS)

$(OUTGUN_DED_EXE): $(OUTGUN_DEDSERV_OBJS)
	$(CXX) $(OUTGUN_LDFLAGS) -o $@ $(OUTGUN_DEDSERV_OBJS) $(OUTGUN_DEDSERV_LIBS)

$(SRVMONIT_EXE): $(MON_OBJS)
	$(CXX) $(MON_LDFLAGS) -o $@ $(MON_OBJS) $(MON_LIBS)

$(RELAY_EXE): $(RELAY_OBJS)
	$(CXX) $(RELAY_LDFLAGS) -o $@ $(RELAY_OBJS) $(RELAY_LIBS)

# -- Helper binaries: --

$(MAKEDEP_EXE): $(MAKEDEP_OBJS)
	$(CXX) $(BUILDTOOL_LDFLAGS) -o $@ $(MAKEDEP_OBJS) $(BUILDTOOL_LIBS)

$(WRITEIFDIFF_EXE): $(WRITEIFDIFF_OBJS)
	$(CXX) $(BUILDTOOL_LDFLAGS) -o $@ $(WRITEIFDIFF_OBJS) $(BUILDTOOL_LIBS)

# -- Testsuite binaries: --

$(TEST_TARGETS) : $(BINDIR)/tests/%$(EXE_SUFFIX) : $(OBJDIR)/tests/%.o $(OBJDIR)/tests/tests.o
	$(CXX) $(TEST_LDFLAGS) -o $@ $^ $(TEST_LIBS)

$(BINDIR)/tests/binarybuffer$(EXE_SUFFIX) : $(OBJDIR)/gui/binaryaccess.o

# -- Executing tests: --

$(TEST_EXEC_TARGETS) : % : $(STATUSDIR)/%

$(TEST_PASS_MARKERS) : $(STATUSDIR)/test_% : $(BINDIR)/tests/%$(EXE_SUFFIX)
	$<
	@echo "passed" > $@

# -- Goals for command line use: --

default: outgun tools

tools: srvmonit relay
all: outgun outgun-ded tools makedep writeifdiff testsuite
ALL: all TAGS run_tests

outgun:      $(OUTGUN_EXE)
outgun-ded:  $(OUTGUN_DED_EXE)
srvmonit:    $(SRVMONIT_EXE)
relay:       $(RELAY_EXE)
makedep:     $(MAKEDEP_EXE)
writeifdiff: $(WRITEIFDIFF_EXE)

testsuite: $(TEST_TARGETS)
run_tests: $(TEST_EXEC_TARGETS)

cleanobjs: # not just object files, but all intermediates
	$(RM) $(GENERATED_HEADER_FILES) $(DEPENDENCY_FILES) $(OBJECTS)

clean: cleanobjs
	$(RM) $(TARGETS) $(TEST_PASS_MARKERS) TAGS

TAGS: $(SRC_FILES)
ifdef LINUX
	etags $^
endif

.PHONY: default tools all ALL outgun outgun-ded srvmonit relay makedep writeifdiff testsuite run_tests $(TEST_EXEC_TARGETS) cleanobjs clean
