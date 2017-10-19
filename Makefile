COMPILER=gcc

ARGTABLE_C := ./argtable/argtable3.c
ARGTABLE_H := ./argtable/argtable3.h

cJSON_C := ./cJSON/cJSON.c
cJSON_H := ./cJSON/cJSON.h

MAIN_C := $(wildcard ./main/*.c)
MAIN_H := $(wildcard ./main/*.h)

OBJ := $(wildcard ./~build/*.o)
          
LIBS    = -l./eclipse-paho-mqtt-c/lib -I./eclipse-paho-mqtt-c/include \
          -l/usr/bin -I/usr/include \
          -l/usr/local/bin -I/usr/local/include 
          

target debug : FLAGS = -Og -g
target release : FLAGS = -O3

#install:
#	rm -f 

mathneon_test: mathneon_a.o mathneon_test.o
	$(COMPILER) $(FLAGS) -o mathtest mathneon_debug.o $(OBJS) -lm

mathneon_a: mathneon_a.o
	ar rcs libmathneon.a $(OBJS)

mathneon_so: mathneon_so.o
	rm -f libmathneon.so*
	$(COMPILER) -shared -Wl,-install_name,libmathneon.so.1 -o libmathneon.so.1.0.0 $(OBJS) -lm
	
mathneon_so.o: $(SOURCES) $(HEADERS)
	$(COMPILER) $(FLAGS) -fPIC -c $(SOURCES) $(HEADERS) 

mathneon_a.o: $(SOURCES) $(HEADERS)
	$(COMPILER) $(FLAGS) -c $(SOURCES) $(HEADERS) -lm
	
mathneon_test.o: mathneon_debug.c $(HEADERS)
	$(COMPILER) $(FLAGS) -c mathneon_debug.c $(HEADERS) -lm

clean:
	rm -rf ./~build
	rm -f *.gch

