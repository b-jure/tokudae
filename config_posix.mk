# Configuration file for building and installing Tokudae
# {=========================================================================
# 			    Version and release
# ==========================================================================
V = 1.0
R = $(V).0
# }{========================================================================
# 				Paths & Installing
# ==========================================================================
# Install paths
INSTALL_ROOT = /usr/local
INSTALL_BIN = $(INSTALL_ROOT)/bin
INSTALL_INC = $(INSTALL_ROOT)/include
INSTALL_LIB = $(INSTALL_ROOT)/lib
INSTALL_MAN = $(INSTALL_ROOT)/man/man1
INSTALL_TMOD = $(INSTALL_ROOT)/share/tokudae/$(V)
INSTALL_CMOD = $(INSTALL_ROOT)/lib/tokudae/$(V)
INSTALL_DOC = $(INSTALL_TMOD)/doc

# Install tool
INSTALL = install -p
INSTALL_EXEC = $(INSTALL) -m 0755
INSTALL_DATA = $(INSTALL) -m 0644
# }{========================================================================
# 			Compiler and Linker Flags
# ==========================================================================
# Use gcc by default
ifeq ($(strip $(CC)),cc)
CC = gcc
endif

# Internal defines used for testing (all of these slow down operations a lot):
# TOKUI_ASSERT => Enables all internal asserts inside Tokudae.
# TOKUI_EMERGENCYGCTESTS => Forces an emergency collection at every single
#                           allocation.
# TOKUI_HARDMEMTESTS => Forces a full collection at all points where the
#                       collector can run.
# TOKUI_HARDSTACKTESTS => forces a reallocation of the stack at every point
#                         where the stack can be reallocated.
# Recommended macro to define for debug builds
# TOKU_USE_APICHECK => enables asserts in the API (consistency checks)

# Warning flags
WARN = -Wfatal-errors -Wall -Wextra -Wconversion
NOWARN = -Wno-char-subscripts
ifeq ($(strip $(CC)),g++)
NOWARN += -Wno-missing-field-initializers -Wno-literal-suffix
endif
WARNINGS = $(WARN) $(NOWARN)

# System flags
SYSCFLAGS = -DTOKU_USE_POSIX
SYSLDFLAGS =
SYSLIBS =

# Release flags
MYCFLAGS = -O2 -march=native -fno-stack-protector -fno-common
MYLDFLAGS =
MYLIBS =
MYOBJS =

# Testing flags
#MYCFLAGS = -O0 -g3 -DTOKU_USE_APICHECK -DTOKUI_ASSERT
#MYLDFLAGS =
#MYLIBS =
#MYOBJS =

# Special flags for compiler modules; -Os reduces code size.
CMCFLAGS = 

# Final flags
CFLAGS += $(WARNINGS) $(SYSCFLAGS) $(MYCFLAGS)
LDFLAGS += $(SYSLDFLAGS) $(MYLDFLAGS)
LIBS += -lm $(SYSLIBS) $(MYLIBS)
# }{========================================================================
# 			Archiver and Other Utilities
# ==========================================================================
AR = ar rcu
RANLIB = ranlib
RM = rm -f
MKDIR = mkdir -p
UNAME = uname
# }{========================================================================
# 			       Targets
# ==========================================================================
TOKUDAE_T = tokudae
TOKUDAEC_T = tokuc
TOKUDAE_A = libtokudae.a
# }=========================================================================
