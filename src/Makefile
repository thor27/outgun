# try to guess platform
# LINUX=1 is probably better on anything but Windows.
# If the XAUTHORITY test doesn't work on your system, use "make LINUX=1"
# or "make WINDOWS=1" to override, or edit this test.
ifdef XAUTHORITY
 ifndef WINDOWS
  LINUX = 1
 endif
endif

BASE_CFLAGS = -W -Wall -O3 -march=pentium -ffast-math

BASE_OBJS = world.o servnet.o server.o commont.o main.o names.o nameauth.o \
	nassert.o globals.o log.o utility.o network.o thread.o gamemod.o debug.o \
	language.o leetnet/ConditionVariable.o leetnet/Mutex.o leetnet/rudp.o \
	leetnet/server.o leetnet/Time.o leetnet/Timer.o leetnet/dlog.o

CLIENT_OBJS = antialias.o graphics.o client.o client_menus.o sounds.o menu.o \
	mappic.o leetnet/client.o

MAKEDEP_BASE_OBJS = tools/makedep.o

GCC = g++
WINDRES = windres

ifdef LINUX

 CP = cp
 RM = rm -f
 EXE_SUFFIX =

 BASE_LIBS = -lNL
 ifdef DEDICATED_SERVER_ONLY
  ALLEG_LIBS =
  ALLEG_CFLAGS =
 else 
  ifdef STATIC_ALLEGRO
   ALLEG_LIBS = `allegro-config --libs --static`
  else
   ALLEG_LIBS = `allegro-config --libs`
  endif
  ALLEG_CFLAGS =
 endif
 STLP_LIBS =
 STLP_CFLAGS =
 PLATFORM_OBJS = platform_unix.o
 EXTRA_OBJS =
 LFLAGS =

else

 CP = copy
 RM = del
 EXE_SUFFIX = .exe

 BASE_LIBS = -lNL -lpthreadGC -lwinmm
 ifdef STATIC_ALLEGRO
  ALLEG_LIBS = -lalleg_s -lkernel32 -luser32 -lgdi32 -lcomdlg32 -lole32 \
	-ldinput -lddraw -ldxguid -ldsound
  ALLEG_CFLAGS = -DALLEGRO_STATICLINK
 else
  ALLEG_LIBS = -lalleg
  ALLEG_CFLAGS =
 endif
 ifdef STLPORT
  STLP_LIBS = -lstlport_mingw32_static
  STLP_CFLAGS = -I$(MINGDIR)/include/stlport -mthreads
 else
  STLP_LIBS =
  STLP_CFLAGS =
 endif
 PLATFORM_OBJS = platform_win.o
 EXTRA_OBJS = icon.res
 LFLAGS = -mwindows

endif

ifdef DEDICATED_SERVER_ONLY
 CFLAGS = $(BASE_CFLAGS) $(STLP_CFLAGS) -DDEDICATED_SERVER_ONLY
 OBJS = $(BASE_OBJS) $(PLATFORM_OBJS) $(EXTRA_OBJS)
 LIBS = $(BASE_LIBS) $(STLP_LIBS)
else
 CFLAGS = $(BASE_CFLAGS) $(ALLEG_CFLAGS) $(STLP_CFLAGS)
 OBJS = $(BASE_OBJS) $(CLIENT_OBJS) $(PLATFORM_OBJS) $(EXTRA_OBJS)
 LIBS = $(BASE_LIBS) $(ALLEG_LIBS) $(STLP_LIBS)
endif

MAKEDEP_OBJS = $(MAKEDEP_BASE_OBJS)
MAKEDEP_LIBS = $(STLP_LIBS)

all: ../outgun$(EXE_SUFFIX) tools/makedep$(EXE_SUFFIX)

../outgun$(EXE_SUFFIX): $(OBJS)
	$(GCC) $(LFLAGS) -o $@ $(OBJS) $(LIBS)

tools/makedep$(EXE_SUFFIX): $(MAKEDEP_OBJS)
	$(GCC) $(LFLAGS) -o $@ $(MAKEDEP_OBJS) $(MAKEDEP_LIBS)

%.res: %.rc
	$(WINDRES) -i $< -O coff -o $@

OBJCOMPILE = $(GCC) $(CFLAGS) -c -o $@ $<
LEETNETCOMPILE = $(OBJCOMPILE)

include Makefile.dependencies

PHONY: clean

clean:
	$(RM) $(OBJS) $(MAKEDEP_OBJS)
