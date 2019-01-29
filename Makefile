CC=gcc

THISMACHINE := $(shell uname -srm | sed -e 's/ /-/g')
THISSYSTEM	:= $(shell uname -s)

APP         ?= otter
APPDIR      := bin/$(THISMACHINE)
BUILDDIR    := build/$(THISMACHINE)
PKGDIR      := ../_hbpkg/$(THISMACHINE)
SYSDIR      := ../_hbsys/$(THISMACHINE)
EXT_DEF     ?= 
EXT_INC     ?= 
EXT_LIBFLAGS ?= 
EXT_LIBS    ?= 
VERSION     ?= 0.9.0

# Make sure the LD_LIBRARY_PATH includes the _hbsys directory
ifneq ($(findstring $(SYSDIR)/lib,$(LD_LIBRARY_PATH)),)
	error "$(SYSDIR)/lib not in LD_LIBRARY_PATH.  Please update your settings to include this."
endif

ifeq ($(THISSYSTEM),Linux)
	LIBBSD := -lbsd
else
	LIBBSD :=
endif

# Try to get git HEAD commit value
GITHEAD := $(shell git rev-parse --short HEAD)

DEFAULT_DEF := -D__HBUILDER__ -DOTTER_PARAM_GITHEAD=\"$(GITHEAD)\"
LIBMODULES  := argtable cJSON cmdtab bintex m2def libjudy OTEAX libotfs hbuilder-lib $(EXT_LIBS)
SUBMODULES  := cmds main test

SRCEXT      := c
DEPEXT      := d
OBJEXT      := o

CFLAGS_DEBUG:= -std=gnu99 -O -g -Wall -pthread
CFLAGS      := -std=gnu99 -O3 -pthread
#INC         := -I. -I./$(PKGDIR)/argtable -I./$(PKGDIR)/bintex -I./$(PKGDIR)/cJSON -I./$(PKGDIR)/cmdtab -I./$(PKGDIR)/hbuilder -I./$(PKGDIR)/liboteax -I./$(PKGDIR)/libotfs -I./$(PKGDIR)/m2def
INC         := -I. -I./include -I./$(SYSDIR)/include
INCDEP      := -I.
LIBINC      := -L./$(SYSDIR)/lib
LIB         := -largtable -lbintex -lcJSON -lcmdtab -lsmut -lhbuilder -lotfs -loteax -ljudy -lm -lc $(LIBBSD)

OTTER_PKG   := $(PKGDIR)
OTTER_DEF   := $(DEFAULT_DEF) $(EXT_DEF)
OTTER_INC   := $(INC) $(EXT_INC)
OTTER_LIBINC:= $(LIBINC)
OTTER_LIB   := $(EXT_LIBFLAGS) $(LIB)
OTTER_BLD   := $(BUILDDIR)
OTTER_APP   := $(APPDIR)

#OBJECTS     := $(shell find $(BUILDDIR) -type f -name "*.$(OBJEXT)")
#MODULES     := $(SUBMODULES) $(LIBMODULES)

# Export the following variables to the shell: will affect submodules
export OTTER_PKG
export OTTER_DEF
export OTTER_LIBINC
export OTTER_INC
export OTTER_LIB
export OTTER_BLD
export OTTER_APP

deps: $(LIBMODULES)
all: directories $(APP)
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
	$(eval OBJECTS := $(shell find $(BUILDDIR) -type f -name "*.$(OBJEXT)"))
	$(CC) $(CFLAGS_DEBUG) $(OTTER_DEF) -D__DEBUG__ $(OTTER_INC) $(OTTER_LIBINC) -o $(APPDIR)/$(APP).debug $(OBJECTS) $(OTTER_LIB)

#Library dependencies (not in otter sources)
$(LIBMODULES): %: 
#	cd ./../$@ && $(MAKE) lib && $(MAKE) install
	cd ./../$@ && $(MAKE) pkg

#otter submodules
$(SUBMODULES): %: directories
	cd ./$@ && $(MAKE) -f $@.mk obj

#Non-File Targets
.PHONY: all pkg remake clean cleaner

