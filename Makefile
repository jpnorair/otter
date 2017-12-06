CC=gcc

TARGET      ?= otter
TARGETDIR   ?= bin
EXT_DEF     ?= 
EXT_INC     ?= 
EXT_LIBS    ?= 

DEFAULT_DEF := -D__HBUILDER__
LIBMODULES  := bintex hbuilder-lib $(EXT_LIBS)
SUBMODULES  := argtable cJSON main test

BUILDDIR    := build

SRCEXT      := c
DEPEXT      := d
OBJEXT      := o

#CFLAGS      := -std=gnu99 -O -g -Wall -pthread
CFLAGS      := -std=gnu99 -O3 -pthread
INC         := -I$(INCDIR)
INCDEP      := -I$(INCDIR)
LIB         := -lhbuilder -lbintex -L./../hbuilder-lib -L./../bintex
OTTER_DEF   := $(DEFAULT_DEF) $(EXT_DEF)
OTTER_INC   := $(INC) $(EXT_INC)
OTTER_LIB   := $(LIB) $(EXT_LIBS)

#OBJECTS     := $(shell find $(BUILDDIR) -type f -name "*.$(OBJEXT)")
#MODULES     := $(SUBMODULES) + $(LIBMODULES)


# Export the following variables to the shell: will affect submodules
export OTTER_DEF
export OTTER_INC
export OTTER_LIB


all: directories $(TARGET)
remake: cleaner all
obj: $(SUBMODULES) $(LIBMODULES) 

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

#Library dependencies (not in otter sources)
$(LIBMODULES): %: 
	cd ./../$@ && $(MAKE) all

#otter submodules
$(SUBMODULES): %: $(LIBMODULES) directories
	cd ./$@ && $(MAKE) -f $@.mk obj

#Non-File Targets
.PHONY: all remake clean cleaner

