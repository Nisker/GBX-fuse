CC=gcc
CFLAGS= -Wall `pkg-config fuse3 --cflags --libs`
SRC =  fuse.c gbxcart.c setup.c rs232/rs232.c
OUTPUT = gbxfuse

DEPS =  gbxcart.h rs232/rs232.h setup.h
OBJ = fuse.o gbxcart.o rs232/rs232.o setup.o


$(OUTPUT): 
	$(CC) $(SRC) $(CFLAGS) -o $@

clean: 
	rm $(OBJ) $(OUTPUT)

%.o: %.c $(DEPS)
	$(CC) -c -o $@ $< $(CFLAGS)

all: $(OBJ)
	$(CC) -o $(OUTPUT) $^ $(CFLAGS)
