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

OBJ := $(wildcard ./*.o)
          
SEARCH := -l./../HBuilder-lib -I./../HBuilder-lib -I./../bintex -I./../m2def -I./cJSON -I./argtable -I./main -I./test


#FLAGS = -O -g -Wall
FLAGS = -O3 -Wall

all: otter

otter: otter.o
	$(COMPILER) $(FLAGS) $(OBJ) -L./../HBuilder-lib -lhbuilder -o otter.out

otter.o: $(SOURCES) $(HEADERS)
	$(COMPILER) $(FLAGS) $(SEARCH) -c $(SOURCES) $(HEADERS)

clean:
	rm -rf ./*.o
	rm -f ./*.gch
	rm -f ./argtable/*.gch
	rm -f ./cJSON/*.gch
	rm -f ./main/*.gch
