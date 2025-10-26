# Makefile for installing Tokudae

PLATFORMS = linux linux_readline mingw posix
PLATFORM ::= $(strip $(PLATFORM))

# By default, assume linux_readline
ifeq ($(PLATFORM),)
PLATFORM ::= linux_readline
endif

ifeq ($(filter $(PLATFORM),$(PLATFORMS)),$(PLATFORM))
include config_$(PLATFORM).mk
else
$(error Invalid PLATFORM '$(PLATFORM)'. Must be one of: {$(PLATFORMS)})
endif

CORE_O = src/tapi.o src/tlist.o src/tcode.o src/tdebug.o src/tfunction.o\
	 src/tgc.o src/ttable.o src/tlexer.o src/tmem.o src/tmeta.o\
	 src/tobject.o src/tparser.o src/tvm.o src/tprotected.o src/treader.o\
	 src/tstate.o src/tstring.o src/tmarshal.o
LIB_O = src/tokudaeaux.o src/tbaselib.o src/tloadlib.o src/tokudaelib.o\
	src/tstrlib.o src/tmathlib.o src/tiolib.o src/toslib.o src/treglib.o\
	src/tdblib.o src/tlstlib.o src/tutf8lib.o
BASE_O = $(CORE_O) $(LIB_O) $(MYOBJS)

TOKUDAE_O = src/tokudae.o
TOKUDAEC_O = src/tokuc.o

ALL_O= $(BASE_O) $(TOKUDAE_O) $(TOKUDAEC_O)
ALL_T= $(TOKUDAE_A) $(TOKUDAE_T) $(TOKUDAEC_T)
ALL_A= $(TOKUDAE_A)

# What to install.
TO_BIN = $(TOKUDAE_T) $(TOKUDAEC_T)
TO_INC = src/tokudae.h src/tokudaeconf.h src/tokudaelib.h src/tokudaeaux.h\
	 src/tokudaelimits.h src/tokudae.hpp
TO_LIB = $(TOKUDAE_A)
TO_MAN = doc/tokudae.1 doc/tokuc.1
TO_DOC = doc/manual.html doc/manual.css doc/contents.html doc/contents.css \
	 doc/tokudae.css doc/grammar.ebnf

all: $(ALL_T) 

o: $(ALL_O)

a: $(ALL_A)

$(TOKUDAE_A): $(BASE_O)
	$(AR) $@ $(BASE_O)
	$(RANLIB) $@

$(TOKUDAE_T): $(TOKUDAE_O) $(TOKUDAE_A)
	$(CC) -o $@ $(LDFLAGS) $(TOKUDAE_O) $(TOKUDAE_A) $(LIBS)

$(TOKUDAEC_T): $(TOKUDAEC_O) $(TOKUDAE_A)
	$(CC) -o $@ $(LDFLAGS) $(TOKUDAEC_O) $(TOKUDAE_A) $(LIBS)

clean:
	$(RM) $(ALL_T) $(ALL_O)

depend:
	@$(CC) $(CFLAGS) -MM src/t*.c

platforms:
	@echo $(PLATFORMS)

# Echo all config parameters.
echo: echobuild
	@echo "V = $V"
	@echo "R = $R"
	@echo "TO_BIN = $(TO_BIN)"
	@echo "TO_INC = $(TO_INC)"
	@echo "TO_LIB = $(TO_LIB)"
	@echo "TO_MAN = $(TO_MAN)"
	@echo "INSTALL_ROOT = $(INSTALL_ROOT)"
	@echo "INSTALL_BIN = $(INSTALL_BIN)"
	@echo "INSTALL_INC = $(INSTALL_INC)"
	@echo "INSTALL_LIB = $(INSTALL_LIB)"
	@echo "INSTALL_MAN = $(INSTALL_MAN)"
	@echo "INSTALL_TMOD = $(INSTALL_TMOD)"
	@echo "INSTALL_CMOD = $(INSTALL_CMOD)"
	@echo "INSTALL_EXEC = $(INSTALL_EXEC)"
	@echo "INSTALL_DATA = $(INSTALL_DATA)"

echobuild:
	@echo "PLATFORM = $(PLATFORM)"
	@echo "CC = $(CC)"
	@echo "CFLAGS = $(CFLAGS)"
	@echo "LDFLAGS = $(LDFLAGS)"
	@echo "LIBS = $(LIBS)"
	@echo "AR = $(AR)"
	@echo "RANLIB = $(RANLIB)"
	@echo "RM = $(RM)"
	@echo "UNAME = $(UNAME)"

install:
	$(MKDIR) $(INSTALL_BIN) $(INSTALL_INC) $(INSTALL_LIB) \
		$(INSTALL_MAN) $(INSTALL_TMOD) $(INSTALL_CMOD) $(INSTALL_DOC)
	$(INSTALL_EXEC) $(TO_BIN) $(INSTALL_BIN)
	$(INSTALL_DATA) $(TO_INC) $(INSTALL_INC)
	$(INSTALL_DATA) $(TO_LIB) $(INSTALL_LIB)
	$(INSTALL_DATA) $(TO_MAN) $(INSTALL_MAN)
	$(INSTALL_DATA) $(TO_DOC) $(INSTALL_DOC)

uninstall:
	cd $(INSTALL_BIN) && $(RM) $(TO_BIN)
	cd $(INSTALL_INC) && $(RM) $(TO_INC)
	cd $(INSTALL_LIB) && $(RM) $(TO_LIB)
	cd $(INSTALL_MAN) && $(RM) $(TO_MAN)
	cd $(INSTALL_DOC) && $(RM) $(TO_DOC)

local:
	$(MAKE) install INSTALL_ROOT=./local

help:
	@echo "Do 'make' to build targets, or:"
	@echo "platforms  - print available platforms"
	@echo "echo       - print build and configuration parameters"
	@echo "echobuild  - print build parameters"
	@echo "clean      - remove build artifacts"
	@echo "local      - install the distribution in the current directory"
	@echo "install    - install the distribution on to the system"
	@echo "uninstall  - uninstall the distribution from the system"

# Targets that do not create files
.PHONY: all help clean install uninstall local dummy echo o a depend echobuild

# Compiler modules may use special flags.
tlexer.o:
	$(CC) $(CFLAGS) $(CMCFLAGS) -c tlexer.c

tparser.o:
	$(CC) $(CFLAGS) $(CMCFLAGS) -c tparser.c

tcode.o:
	$(CC) $(CFLAGS) $(CMCFLAGS) -c tcode.c

# DO NOT DELETE
tapi.o: src/tapi.c src/tokudaeprefix.h src/tapi.h src/tokudaelimits.h \
 src/tokudae.h src/tokudaeconf.h src/tstate.h src/tobject.h src/tlist.h \
 src/tmeta.h src/tdebug.h src/tfunction.h src/tcode.h src/tbits.h \
 src/tparser.h src/tlexer.h src/treader.h src/tmem.h src/tgc.h \
 src/tmarshal.h src/tprotected.h src/tstring.h src/ttable.h src/tvm.h \
 src/topnames.h
tbaselib.o: src/tbaselib.c src/tokudaeprefix.h src/tokudae.h \
 src/tokudaeconf.h src/tokudaeaux.h src/tokudaelib.h src/tokudaelimits.h
tcode.o: src/tcode.c src/tokudaeprefix.h src/tcode.h src/tbits.h \
 src/tparser.h src/tlexer.h src/treader.h src/tokudae.h src/tokudaeconf.h \
 src/tmem.h src/tokudaelimits.h src/tobject.h src/ttable.h src/tdebug.h \
 src/tstate.h src/tlist.h src/tmeta.h src/tvm.h src/tgc.h
tdblib.o: src/tdblib.c src/tokudaeprefix.h src/tokudae.h \
 src/tokudaeconf.h src/tokudaeaux.h src/tokudaelib.h src/tokudaelimits.h
tdebug.o: src/tdebug.c src/tokudaeprefix.h src/tokudae.h \
 src/tokudaeconf.h src/tdebug.h src/tobject.h src/tokudaelimits.h \
 src/tstate.h src/tlist.h src/tmeta.h src/tapi.h src/tcode.h src/tbits.h \
 src/tparser.h src/tlexer.h src/treader.h src/tmem.h src/tfunction.h \
 src/tstring.h src/tprotected.h src/tvm.h src/tgc.h src/ttable.h
tfunction.o: src/tfunction.c src/tokudaeprefix.h src/tfunction.h \
 src/tcode.h src/tbits.h src/tparser.h src/tlexer.h src/treader.h \
 src/tokudae.h src/tokudaeconf.h src/tmem.h src/tokudaelimits.h \
 src/tobject.h src/tstate.h src/tlist.h src/tmeta.h src/tdebug.h \
 src/tgc.h src/tvm.h src/tprotected.h
tgc.o: src/tgc.c src/tokudaeprefix.h src/tgc.h src/tbits.h src/tobject.h \
 src/tokudae.h src/tokudaeconf.h src/tokudaelimits.h src/tstate.h \
 src/tlist.h src/tmeta.h src/tfunction.h src/tcode.h src/tparser.h \
 src/tlexer.h src/treader.h src/tmem.h src/ttable.h src/tstring.h \
 src/tvm.h src/tprotected.h
tiolib.o: src/tiolib.c src/tokudaeprefix.h src/tokudae.h \
 src/tokudaeconf.h src/tokudaeaux.h src/tokudaelib.h src/tokudaelimits.h
tlexer.o: src/tlexer.c src/tokudaeprefix.h src/tobject.h src/tokudae.h \
 src/tokudaeconf.h src/tokudaelimits.h src/tgc.h src/tbits.h src/tstate.h \
 src/tlist.h src/tmeta.h src/tlexer.h src/treader.h src/tmem.h \
 src/tdebug.h src/tprotected.h src/ttable.h src/tstring.h
tlist.o: src/tlist.c src/tokudaeprefix.h src/tlist.h src/tobject.h \
 src/tokudae.h src/tokudaeconf.h src/tokudaelimits.h src/tstring.h \
 src/tstate.h src/tmeta.h src/tlexer.h src/treader.h src/tmem.h src/tgc.h \
 src/tbits.h src/tdebug.h
tloadlib.o: src/tloadlib.c src/tokudaeprefix.h src/tokudae.h \
 src/tokudaeconf.h src/tokudaeaux.h src/tokudaelib.h src/tokudaelimits.h
tlstlib.o: src/tlstlib.c src/tokudaeprefix.h src/tokudae.h \
 src/tokudaeconf.h src/tokudaeaux.h src/tokudaelib.h src/tokudaelimits.h
tmarshal.o: src/tmarshal.c src/tokudaeprefix.h src/tcode.h src/tbits.h \
 src/tparser.h src/tlexer.h src/treader.h src/tokudae.h src/tokudaeconf.h \
 src/tmem.h src/tokudaelimits.h src/tobject.h src/tfunction.h \
 src/tstate.h src/tlist.h src/tmeta.h src/tgc.h src/tmarshal.h \
 src/tprotected.h src/tstring.h src/ttable.h
tmathlib.o: src/tmathlib.c src/tokudaeprefix.h src/tokudae.h \
 src/tokudaeconf.h src/tokudaeaux.h src/tokudaelib.h src/tokudaelimits.h
tmem.o: src/tmem.c src/tokudaeprefix.h src/tgc.h src/tbits.h \
 src/tobject.h src/tokudae.h src/tokudaeconf.h src/tokudaelimits.h \
 src/tstate.h src/tlist.h src/tmeta.h src/tdebug.h src/tmem.h \
 src/tprotected.h src/treader.h
tmeta.o: src/tmeta.c src/tokudaeprefix.h src/tmeta.h src/tokudaeconf.h \
 src/tokudae.h src/tobject.h src/tokudaelimits.h src/tlist.h src/tlexer.h \
 src/treader.h src/tmem.h src/tstring.h src/tstate.h src/tdebug.h \
 src/ttable.h src/tbits.h src/tgc.h src/tvm.h src/tprotected.h
tobject.o: src/tobject.c src/tokudaeprefix.h src/tokudaelimits.h \
 src/tokudae.h src/tokudaeconf.h src/tobject.h src/tvm.h src/tstate.h \
 src/tlist.h src/tmeta.h
tokuc.o: src/tokuc.c src/tokudaeprefix.h src/tokudae.h src/tokudaeconf.h \
 src/tokudaeaux.h src/tokudaelimits.h
tokudaeaux.o: src/tokudaeaux.c src/tokudaeprefix.h src/tokudae.h \
 src/tokudaeconf.h src/tokudaeaux.h src/tokudaelimits.h
tokudae.o: src/tokudae.c src/tokudaeprefix.h src/tokudae.h \
 src/tokudaeconf.h src/tokudaeaux.h src/tokudaelib.h src/tokudaelimits.h
tokudaelib.o: src/tokudaelib.c src/tokudaeprefix.h src/tokudaelib.h \
 src/tokudae.h src/tokudaeconf.h src/tokudaeaux.h
toslib.o: src/toslib.c src/tokudaeprefix.h src/tokudae.h \
 src/tokudaeconf.h src/tokudaeaux.h src/tokudaelib.h src/tokudaelimits.h
tparser.o: src/tparser.c src/tokudaeprefix.h src/tcode.h src/tbits.h \
 src/tparser.h src/tlexer.h src/treader.h src/tokudae.h src/tokudaeconf.h \
 src/tmem.h src/tokudaelimits.h src/tobject.h src/tfunction.h \
 src/tstate.h src/tlist.h src/tmeta.h src/tgc.h src/tstring.h \
 src/ttable.h src/tvm.h
tprotected.o: src/tprotected.c src/tokudaeprefix.h src/tfunction.h \
 src/tcode.h src/tbits.h src/tparser.h src/tlexer.h src/treader.h \
 src/tokudae.h src/tokudaeconf.h src/tmem.h src/tokudaelimits.h \
 src/tobject.h src/tstate.h src/tlist.h src/tmeta.h src/tgc.h \
 src/tmarshal.h src/tprotected.h src/tstring.h
treader.o: src/treader.c src/tokudaeprefix.h src/treader.h src/tokudae.h \
 src/tokudaeconf.h src/tmem.h src/tokudaelimits.h
treglib.o: src/treglib.c src/tokudaeprefix.h src/tokudae.h \
 src/tokudaeconf.h src/tstrlib.h src/tokudaelimits.h src/tokudaeaux.h \
 src/tokudaelib.h
tstate.o: src/tstate.c src/tokudaeprefix.h src/ttable.h src/tobject.h \
 src/tokudae.h src/tokudaeconf.h src/tokudaelimits.h src/tbits.h \
 src/tlist.h src/tstate.h src/tmeta.h src/tapi.h src/tdebug.h \
 src/tfunction.h src/tcode.h src/tparser.h src/tlexer.h src/treader.h \
 src/tmem.h src/tgc.h src/tprotected.h src/tstring.h
tstring.o: src/tstring.c src/tokudaeprefix.h src/tstate.h src/tobject.h \
 src/tokudae.h src/tokudaeconf.h src/tokudaelimits.h src/tlist.h \
 src/tmeta.h src/tstring.h src/tlexer.h src/treader.h src/tmem.h \
 src/tgc.h src/tbits.h src/tdebug.h src/tvm.h src/tprotected.h
tstrlib.o: src/tstrlib.c src/tokudaeprefix.h src/tokudae.h \
 src/tokudaeconf.h src/tstrlib.h src/tokudaelimits.h src/tokudaeaux.h \
 src/tokudaelib.h
ttable.o: src/ttable.c src/tokudaeprefix.h src/tstring.h src/tobject.h \
 src/tokudae.h src/tokudaeconf.h src/tokudaelimits.h src/tstate.h \
 src/tlist.h src/tmeta.h src/tlexer.h src/treader.h src/tmem.h \
 src/ttable.h src/tbits.h src/tgc.h src/tdebug.h
tutf8lib.o: src/tutf8lib.c src/tokudaeprefix.h src/tokudae.h \
 src/tokudaeconf.h src/tokudaeaux.h src/tokudaelib.h src/tokudaelimits.h
tvm.o: src/tvm.c src/tokudaeprefix.h src/tapi.h src/tokudaelimits.h \
 src/tokudae.h src/tokudaeconf.h src/tstate.h src/tobject.h src/tlist.h \
 src/tmeta.h src/tfunction.h src/tcode.h src/tbits.h src/tparser.h \
 src/tlexer.h src/treader.h src/tmem.h src/tgc.h src/ttable.h \
 src/tdebug.h src/tvm.h src/tstring.h src/tprotected.h src/tjmptable.h
