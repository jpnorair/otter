CC=gcc

TARGET      := main
OTTER_PKG   ?=
OTTER_DEF   ?= -D__HBUILDER__
OTTER_INC   ?=
OTTER_LIB   ?= 

#CFLAGS      := -std=gnu99 -O -g -Wall -pthread 
CFLAGS      := -std=gnu99 -O3 -pthread

BUILDDIR    := ../build/$(TARGET)
TARGETDIR   := .
SRCEXT      := c
DEPEXT      := d
OBJEXT      := o
#LIB         := -lhbuilder -lcmdtab -lbintex -L./../$(OTTER_PKG)/hbuilder -L./../$(OTTER_PKG)/cmdtab -L./../$(OTTER_PKG)/bintex
LIB			:= $(subst -L./,-L./../,$(OTTER_LIB))
#INC         := -I./../argtable -I./../cJSON -I./../test -I./../$(OTTER_PKG)/m2def -I./../$(OTTER_PKG)/hbuilder -I./../$(OTTER_PKG)/cmdtab -I./../$(OTTER_PKG)/bintex
INC			:= $(subst -I./,-I./../,$(OTTER_INC)) -I./../test -I./../cJSON
INCDEP      := $(INC)

SOURCES     := $(shell find . -type f -name "*.$(SRCEXT)")
OBJECTS     := $(patsubst ./%,$(BUILDDIR)/%,$(SOURCES:.$(SRCEXT)=.$(OBJEXT)))


all: resources $(TARGET)
obj: $(OBJECTS)
remake: cleaner all


#Copy Resources from Resources Directory to Target Directory
resources: directories

#Make the Directories
directories:
	@mkdir -p $(TARGETDIR)
	@mkdir -p $(BUILDDIR)

#Clean only Objects
clean:
	@$(RM) -rf $(BUILDDIR)

#Full Clean, Objects and Binaries
cleaner: clean
	@$(RM) -rf $(TARGETDIR)

#Pull in dependency info for *existing* .o files
-include $(OBJECTS:.$(OBJEXT)=.$(DEPEXT))

#Direct build of the test app with objects
$(TARGET): $(OBJECTS)
	$(CC) -o $(TARGETDIR)/$(TARGET) $^ $(LIB)

#Compile Stages
$(BUILDDIR)/%.$(OBJEXT): ./%.$(SRCEXT)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(OTTER_DEF) $(INC) -c -o $@ $<
	@$(CC) $(CFLAGS) $(OTTER_DEF) $(INCDEP) -MM ./$*.$(SRCEXT) > $(BUILDDIR)/$*.$(DEPEXT)
	@cp -f $(BUILDDIR)/$*.$(DEPEXT) $(BUILDDIR)/$*.$(DEPEXT).tmp
	@sed -e 's|.*:|$(BUILDDIR)/$*.$(OBJEXT):|' < $(BUILDDIR)/$*.$(DEPEXT).tmp > $(BUILDDIR)/$*.$(DEPEXT)
	@sed -e 's/.*://' -e 's/\\$$//' < $(BUILDDIR)/$*.$(DEPEXT).tmp | fmt -1 | sed -e 's/^ *//' -e 's/$$/:/' >> $(BUILDDIR)/$*.$(DEPEXT)
	@rm -f $(BUILDDIR)/$*.$(DEPEXT).tmp

#Non-File Targets
.PHONY: all remake clean cleaner resources

