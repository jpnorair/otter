CC=gcc

TARGET      := otter
OTTER_DEF   ?= -D__HBUILDER__

LIBMODULES  := bintex hbuilder-lib
SUBMODULES  := argtable cJSON main test

BUILDDIR    := build
TARGETDIR   := bin
SRCEXT      := c
DEPEXT      := d
OBJEXT      := o

#CFLAGS      := -std=gnu99 -O -g -Wall -pthread
CFLAGS      := -std=gnu99 -O3 -pthread
LIB         := -lhbuilder -lbintex -L./../hbuilder-lib -L./../bintex
INC         := -I$(INCDIR)
INCDEP      := -I$(INCDIR)
          
#OBJECTS     := $(shell find $(BUILDDIR) -type f -name "*.$(OBJEXT)")
#MODULES     := $(SUBMODULES) + $(LIBMODULES)


# Export the following variables to the shell: will affect submodules
export OTTER_DEF


all: directories $(TARGET)
remake: cleaner all
	

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
	$(CC) $(CFLAGS) $(OTTER_DEF) -o $(TARGETDIR)/$(TARGET) $(OBJECTS) $(LIB)

$(LIBMODULES): %: 
	cd ./../$@ && $(MAKE) all
	
$(SUBMODULES): %: $(LIBMODULES) directories
	cd ./$@ && $(MAKE) -f $@.mk obj

#Non-File Targets
.PHONY: all remake clean cleaner


