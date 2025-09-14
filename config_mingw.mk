# Configuration file for building and installing Tokudae on mingw


# {=========================================================================
# 			    Version and release
# ==========================================================================
V = 1.0
R = $(V).0
# }=========================================================================


# {=========================================================================
# 				Paths & Installing
# ==========================================================================
# Your platform (see PLATFORMS for possible values).
PLATFORM = mingw
PLATFORMS = guess posix linux linux-readline mingw generic 

# Install paths
INSTALL_ROOT = /usr/local
INSTALL_BIN = $(INSTALL_ROOT)/bin
INSTALL_INC = $(INSTALL_ROOT)/include
INSTALL_LIB = $(INSTALL_ROOT)/lib
INSTALL_MAN = $(INSTALL_ROOT)/man/man1
INSTALL_TMOD = $(INSTALL_ROOT)/share/tokudae/$(V)
INSTALL_CMOD = $(INSTALL_ROOT)/lib/tokudae/$(V)

# Install tool
INSTALL = install -p
INSTALL_EXEC = $(INSTALL) -m 0755
INSTALL_DATA = $(INSTALL) -m 0644
# }=========================================================================


# {=========================================================================
# 			Compiler and Linker Flags
# ==========================================================================
# Internal defines used for testing (all of these slow down operations a lot):
# -DTOKUI_ASSERT => Enables all internal asserts inside Tokudae.
# -DTOKUI_TRACE_EXEC => Traces bytecode execution (including stack state).
# -DTOKUI_DISASSEMBLE_BYTECODE => Disassembles precompiled chunks.
# -DTOKUI_EMERGENCYGCTESTS => Forces an emergency collection at every single
# 			      allocation.
# -DTOKUI_HARDMEMTESTS => Forces a full collection at all points where the
# 			  collector can run.
# -DTOKUI_HARDSTACKTESTS => forces a reallocation of the stack at every point
# 			    where the stack can be reallocated.
#
# Recommended macro to define for debug builds
# -TOKU_USE_APICHECK => enables asserts in the API (consistency checks)

# handy when compiling with g++
ifeq ($(strip $(CC)),)
CC = gcc
else ifeq ($(strip $(CC)),g++)
CFLAGS = -Wno-missing-field-initializers -Wno-literal-suffix
endif

CFLAGS = -Wfatal-errors -Wall -Wextra -Werror -Wconversion $(SYSCFLAGS) $(MYCFLAGS)
LDFLAGS = $(SYSLDFLAGS) $(MYLDFLAGS)
LIBS = -lm $(SYSLIBS) $(MYLIBS)

# system flags
SYSCFLAGS = -DTOKU_BUILD_AS_DLL
SYSLDFLAGS = -s
SYSLIBS =

# Release flags
MYCFLAGS = -O2 -march=native -fno-stack-protector -fno-common
MYLDFLAGS =
MYLIBS =
MYOBJS =

# Testing flags
#ASANFLAGS = -fsanitize=address -fsanitize=undefined \
# 	    -fsanitize=pointer-subtract -fsanitize=pointer-compare
#MYCFLAGS = $(ASANFLAGS) -O0 -g3 -DTOKU_USE_APICHECK -DTOKUI_ASSERT
#	   #-DTOKUI_DISASSEMBLE_BYTECODE -DTOKUI_TRACE_EXEC
#MYLDFLAGS = $(ASANFLAGS)
#MYLIBS =
#MYOBJS =

# Special flags for compiler modules; -Os reduces code size.
CMCFLAGS= 
# }=========================================================================


# {=========================================================================
# 			Archiver and Other Utilities
# ==========================================================================
ifeq ($(CC),g++)
AR = g++ -shared -o
else
AR = gcc -shared -o
endif

RANLIB = strip --strip-unneeded
RM = rm -f
MKDIR = mkdir -p
UNAME = uname
# }=========================================================================


# {=========================================================================
# 			       Targets
# ==========================================================================
TOKUDAE_T = tokudae.exe
TOKUDAE_A = tokudae1.dll
# }=========================================================================
