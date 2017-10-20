COMPILER=gcc

ARGTABLE_C := ./argtable/argtable3.c
ARGTABLE_H := ./argtable/argtable3.h

cJSON_C := ./cJSON/cJSON.c
cJSON_H := ./cJSON/cJSON.h

BINTEX_C := ./../bintex/bintex.c
BINTEX_H := ./../bintex/bintex.h

MAIN_C := $(wildcard ./main/*.c)
MAIN_H := $(wildcard ./main/*.h)

M2DEF_H := ./../m2def/m2def.h

TEST_C := $(wildcard ./test/*.c)
TEST_H := $(wildcard ./test/*.h)

SOURCES := $(ARGTABLE_C) $(cJSON_C) $(MAIN_C) $(BINTEX_C)
HEADERS := $(ARGTABLE_H) $(cJSON_H) $(MAIN_H) $(BINTEX_H) $(M2DEF_H) $(TEST_H)
     
SEARCH := -I./../HBuilder-lib \
          -I./../m2def -I./../bintex \
          -I./cJSON -I./argtable \
          -I./main -I./test


#FLAGS = -std=gnu99 -O -g -Wall -pthread
FLAGS = -std=gnu99 -O3 -pthread

all: otter

otter: otter.o
	$(eval OBJS := $(shell ls ./*.o))
	$(COMPILER) $(FLAGS) $(OBJS) -I./../HBuilder-lib -L./../HBuilder-lib -lhbuilder -o otter.out

otter.o: $(SOURCES) $(HEADERS)
	$(COMPILER) $(FLAGS) $(SEARCH) -c $(SOURCES) $(HEADERS)

install:
	mv otter.out ./../interlink/linux-mint17-i686/otter

clean:
	rm -rf ./*.o
	rm -f ./*.gch
	rm -f ./argtable/*.gch
	rm -f ./cJSON/*.gch
	rm -f ./main/*.gch
