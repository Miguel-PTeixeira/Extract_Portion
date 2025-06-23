CFLAGS = -g -Wall

LIBS = -lvorbis -lvorbisenc -lvorbisfile -logg -lsndfile

OBJECTS = build/main.o

all: build main
	@echo "\nUSAGE: ./extract_portion *filename.ogg *time_start *duration\n\n"
	
main: $(OBJECTS)
	gcc $(CFLAGS) $^ $(LIBS) -o extract_portion

build/%.o: src/%.c
	gcc $(CFLAGS) -c $< -o $@

build:
	mkdir -p build

clean:
	rm -f build/*.o my_ogg_compress my_ogg_compress2 ex_ogg_compress
clear:
	rm -f data/*.ogg
