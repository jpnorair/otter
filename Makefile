CC=gcc

THISMACHINE := $(shell uname -srm | sed -e 's/ /-/g')
THISSYSTEM	:= $(shell uname -s)

APP      ?= otter
APPDIR   ?= bin
PKGDIR      ?= ../_hbpkg/$(THISMACHINE)
SYSDIR      ?= ../_hbsys/$(THISMACHINE)
EXT_DEF     ?= 
EXT_INC     ?= 
EXT_LIBFLAGS ?= 
EXT_LIBS    ?= 
VERSION     ?= 0.6.0

DEFAULT_DEF := -D__HBUILDER__
LIBMODULES  := argtable cJSON cmdtab bintex hbuilder-lib OTEAX libotfs $(EXT_LIBS)
SUBMODULES  := main test

BUILDDIR    := build

SRCEXT      := c
DEPEXT      := d
OBJEXT      := o

CFLAGS_DEBUG:= -std=gnu99 -O -g -Wall -pthread
CFLAGS      := -std=gnu99 -O3 -pthread
INC         := -I. -I./$(PKGDIR)/argtable -I./$(PKGDIR)/bintex -I./$(PKGDIR)/cJSON -I./$(PKGDIR)/cmdtab -I./$(PKGDIR)/hbuilder -I./$(PKGDIR)/liboteax -I./$(PKGDIR)/libotfs -I./$(PKGDIR)/m2def
INCDEP      := -I.
LIB         := -largtable -lbintex -lcJSON -lcmdtab -lhbuilder -loteax -lotfs -L./$(PKGDIR)/argtable -L./$(PKGDIR)/bintex -L./$(PKGDIR)/cJSON -L./$(PKGDIR)/cmdtab -L./$(PKGDIR)/hbuilder -L./$(PKGDIR)/liboteax -L./$(PKGDIR)/libotfs
OTTER_PKG   := $(PKGDIR)
OTTER_DEF   := $(DEFAULT_DEF) $(EXT_DEF)
OTTER_INC   := $(INC) $(EXT_INC)
OTTER_LIB   := $(LIB) $(EXT_LIBFLAGS)

#OBJECTS     := $(shell find $(BUILDDIR) -type f -name "*.$(OBJEXT)")
#MODULES     := $(SUBMODULES) $(LIBMODULES)

# Export the following variables to the shell: will affect submodules
export OTTER_PKG
export OTTER_DEF
export OTTER_INC
export OTTER_LIB


all: directories $(APP)
debug: directories $(APP).debug
obj: $(SUBMODULES) $(LIBMODULES) 
remake: cleaner all


install: 
	@rm -rf $(PKGDIR)/$(APP).$(VERSION)
	@mkdir -p $(PKGDIR)/$(APP).$(VERSION)
	@cp $(APPDIR)/$(APP) $(PKGDIR)/$(APP).$(VERSION)/
	@rm -f $(PKGDIR)/$(APP)
	@ln -s $(APP).$(VERSION) ./$(PKGDIR)/$(APP)

directories:
	@mkdir -p $(APPDIR)
	@mkdir -p $(BUILDDIR)

# Clean only only objects
clean:
	@$(RM) -rf $(BUILDDIR)

# Clean objects and binaries
cleaner: clean
	@$(RM) -rf $(APPDIR)	

#Linker
$(APP): $(SUBMODULES) $(LIBMODULES)
	$(eval OBJECTS := $(shell find $(BUILDDIR) -type f -name "*.$(OBJEXT)"))
	$(CC) $(CFLAGS) $(OTTER_DEF) $(OTTER_INC) $(OTTER_LIB) -o $(APPDIR)/$(APP) $(OBJECTS)

$(APP).debug: $(SUBMODULES) $(LIBMODULES)
	$(eval OBJECTS := $(shell find $(BUILDDIR) -type f -name "*.$(OBJEXT)"))
	$(CC) $(CFLAGS_DEBUG) $(OTTER_DEF) -D__DEBUG__ $(OTTER_INC) $(OTTER_LIB) -o $(APPDIR)/$(APP).debug $(OBJECTS)

#Library dependencies (not in otter sources)
$(LIBMODULES): %: 
	cd ./../$@ && $(MAKE) lib && $(MAKE) install

#otter submodules
$(SUBMODULES): %: $(LIBMODULES) directories
	cd ./$@ && $(MAKE) -f $@.mk obj

#Non-File Targets
.PHONY: all remake clean cleaner

