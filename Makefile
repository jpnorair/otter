CC=gcc

THISMACHINE := $(shell uname -srm | sed -e 's/ /-/g')
THISSYSTEM	:= $(shell uname -s)

TARGET      ?= otter
TARGETDIR   ?= bin
PKGDIR      ?= ../_hbpkg/$(THISMACHINE)
SYSDIR      ?= ../_hbsys
EXT_DEF     ?= 
EXT_INC     ?= 
EXT_LIBFLAGS ?= 
EXT_LIBS ?= 
VERSION     ?= "0.5.0"

DEFAULT_DEF := -D__HBUILDER__
LIBMODULES  := libotfs bintex hbuilder-lib $(EXT_LIBS)
SUBMODULES  := argtable cJSON main test

BUILDDIR    := build

SRCEXT      := c
DEPEXT      := d
OBJEXT      := o

CFLAGS_DEBUG:= -std=gnu99 -O -g -Wall -pthread
CFLAGS      := -std=gnu99 -O3 -pthread
INC         := -I. -I./$(PKGDIR)/libotfs
INCDEP      := -I.
LIB         := -lotfs -lhbuilder -lbintex -L./$(PKGDIR)/libotfs -L./$(PKGDIR)/hbuilder -L./$(PKGDIR)/bintex
OTTER_DEF   := $(DEFAULT_DEF) $(EXT_DEF)
OTTER_INC   := $(INC) $(EXT_INC)
OTTER_LIB   := $(LIB) $(EXT_LIBFLAGS)

#OBJECTS     := $(shell find $(BUILDDIR) -type f -name "*.$(OBJEXT)")
#MODULES     := $(SUBMODULES) $(LIBMODULES)

# Export the following variables to the shell: will affect submodules
export OTTER_DEF
export OTTER_INC
export OTTER_LIB


all: directories $(TARGET)
debug: directories $(TARGET).debug
obj: $(SUBMODULES) $(LIBMODULES) 
remake: cleaner all


install: 
	@mkdir -p $(SYSDIR)/bin
	@mkdir -p $(PKGDIR)
	@cp $(TARGETDIR)/$(TARGET) $(PKGDIR)/bin/
	@rm -f $(SYSDIR)/bin/$(TARGET)
# TODO	@ln -s hbuilder.$(VERSION) ./$(PKGDIR)/otter/bin/$(TARGET)

directories:
	@mkdir -p $(TARGETDIR)
	@mkdir -p $(BUILDDIR)

#Clean only Objects
clean:
	@$(RM) -rf $(BUILDDIR)

#Full Clean, Objects and Binaries
cleaner: clean
	@$(RM) -rf $(TARGETDIR)

#Linker
$(TARGET): $(SUBMODULES) $(LIBMODULES)
	$(eval OBJECTS := $(shell find $(BUILDDIR) -type f -name "*.$(OBJEXT)"))
	$(CC) $(CFLAGS) $(OTTER_DEF) -o $(TARGETDIR)/$(TARGET) $(OBJECTS) $(OTTER_LIB)

$(TARGET).debug: $(SUBMODULES) $(LIBMODULES)
	$(eval OBJECTS := $(shell find $(BUILDDIR) -type f -name "*.$(OBJEXT)"))
	$(CC) $(CFLAGS_DEBUG) $(OTTER_DEF) -D__DEBUG__ -o $(TARGETDIR)/$(TARGET) $(OBJECTS) $(OTTER_LIB)

#Library dependencies (not in otter sources)
$(LIBMODULES): %: 
	cd ./../$@ && $(MAKE) all && $(MAKE) install

#otter submodules
$(SUBMODULES): %: $(LIBMODULES) directories
	cd ./$@ && $(MAKE) -f $@.mk obj

#Non-File Targets
.PHONY: all remake clean cleaner

