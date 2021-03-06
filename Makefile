# Copyright 2018, JP Norair
#
# Licensed under the OpenTag License, Version 1.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
# http://www.indigresso.com/wiki/doku.php?id=opentag:license_1_0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#

CC := gcc
LD := ld

THISMACHINE ?= $(shell uname -srm | sed -e 's/ /-/g')
THISSYSTEM	?= $(shell uname -s)

APP         ?= otter
PKGDIR      := ../_hbpkg/$(THISMACHINE)
SYSDIR      := ../_hbsys/$(THISMACHINE)
EXT_DEF     ?= -DOTTER_FEATURE_MODBUS=0
EXT_INC     ?= 
EXT_LIBFLAGS ?= 
EXT_LIBS    ?= 
VERSION     ?= 1.0.a

# Try to get git HEAD commit value
ifneq ($(INSTALLER_HEAD),)
    GITHEAD := $(INSTALLER_HEAD)
else
    GITHEAD := $(shell git rev-parse --short HEAD)
endif

ifeq ($(MAKECMDGOALS),debug)
	APPDIR      := bin/$(THISMACHINE)
	BUILDDIR    := build/$(THISMACHINE)_debug
	DEBUG_MODE  := 1
else
	APPDIR      := bin/$(THISMACHINE)
	BUILDDIR    := build/$(THISMACHINE)
	DEBUG_MODE  := 0
endif


# Make sure the LD_LIBRARY_PATH includes the _hbsys directory
ifneq ($(findstring $(SYSDIR)/lib,$(LD_LIBRARY_PATH)),)
	error "$(SYSDIR)/lib not in LD_LIBRARY_PATH.  Please update your settings to include this."
endif

ifeq ($(THISSYSTEM),Linux)
	ifneq ($(findstring OpenWRT,$(THISMACHINE)),)
		LIBBSD := -lfts
	else
		LIBBSD := -lbsd
	endif
else
	LIBBSD :=
endif


DEFAULT_DEF := -D__HBUILDER__ -DOTTER_PARAM_GITHEAD=\"$(GITHEAD)\"
LIBMODULES  := argtable cJSON clithread cmdtab otvar bintex OTEAX libotfs $(EXT_LIBS)
SUBMODULES  := cmds main test

SRCEXT      := c
DEPEXT      := d
OBJEXT      := o

CFLAGS_DEBUG?= -std=gnu99 -Og -g -Wall -pthread
CFLAGS      ?= -std=gnu99 -O3 -pthread
#INC         := -I. -I./$(PKGDIR)/argtable -I./$(PKGDIR)/bintex -I./$(PKGDIR)/cJSON -I./$(PKGDIR)/cmdtab -I./$(PKGDIR)/hbuilder -I./$(PKGDIR)/liboteax -I./$(PKGDIR)/libotfs 
INC         := -I. -I./include -I./$(SYSDIR)/include $(EXT_INC)
INCDEP      := -I.
LIBINC      := -L./$(SYSDIR)/lib $(EXT_LIB) 
LIB         := -largtable -lbintex -lcJSON -lclithread -lcmdtab -lotvar -lotfs -loteax -lhbutils -ltalloc -lm -lc $(LIBBSD)

OTTER_PKG   := $(PKGDIR)
OTTER_DEF   := $(DEFAULT_DEF) $(EXT_DEF)
OTTER_INC   := $(INC) $(EXT_INC)
OTTER_LIBINC:= $(LIBINC)
OTTER_LIB   := $(EXT_LIBFLAGS) $(LIB)
OTTER_BLD   := $(BUILDDIR)
OTTER_APP   := $(APPDIR)


# Export the following variables to the shell: will affect submodules
export OTTER_PKG
export OTTER_DEF
export OTTER_LIBINC
export OTTER_INC
export OTTER_LIB
export OTTER_BLD
export OTTER_APP

deps: $(LIBMODULES)
all: release
release: directories $(APP)
debug: directories $(APP).debug
obj: $(SUBMODULES)
pkg: deps all install
remake: cleaner all


install: 
	@rm -rf $(PKGDIR)/$(APP).$(VERSION)
	@mkdir -p $(PKGDIR)/$(APP).$(VERSION)
	@cp $(APPDIR)/$(APP) $(PKGDIR)/$(APP).$(VERSION)/
	@rm -f $(PKGDIR)/$(APP)
	@ln -s $(APP).$(VERSION) ./$(PKGDIR)/$(APP)
	cd ../_hbsys && $(MAKE) sys_install INS_MACHINE=$(THISMACHINE) INS_PKGNAME=otter

directories:
	@mkdir -p $(APPDIR)
	@mkdir -p $(BUILDDIR)

# Clean only this machine
clean:
	@$(RM) -rf $(BUILDDIR)
	@$(RM) -rf $(APPDIR)

# Clean all builds
cleaner: 
	@$(RM) -rf ./build
	@$(RM) -rf ./bin

#Linker
$(APP): $(SUBMODULES) 
	$(eval OBJECTS := $(shell find $(BUILDDIR) -type f -name "*.$(OBJEXT)"))
	$(CC) $(CFLAGS) $(OTTER_DEF) $(OTTER_INC) $(OTTER_LIBINC) -o $(APPDIR)/$(APP) $(OBJECTS) $(OTTER_LIB)

$(APP).debug: $(SUBMODULES)
	$(eval OBJECTS_D := $(shell find $(BUILDDIR) -type f -name "*.$(OBJEXT)"))
	$(CC) $(CFLAGS_DEBUG) $(OTTER_DEF) -D__DEBUG__ $(OTTER_INC) $(OTTER_LIBINC) -o $(APPDIR)/$(APP).debug $(OBJECTS_D) $(OTTER_LIB)

#Library dependencies (not in otter sources)
$(LIBMODULES): %: 
#	cd ./../$@ && $(MAKE) lib && $(MAKE) install
	cd ./../$@ && $(MAKE) pkg

#otter submodules
$(SUBMODULES): %: directories
	cd ./$@ && $(MAKE) -f $@.mk obj EXT_DEBUG=$(DEBUG_MODE)

#Non-File Targets
.PHONY: deps all release debug obj pkg remake install directories clean cleaner

