CFLAGS=-Wall -Werror -O0 -g -DDEBUG=1
#CFLAGS=-Wall -Werror -O3

all: test_liboping

clean:
	rm -f *.o
	rm -f test_liboping

liboping.o: liboping.c liboping.h

test_liboping.o: test_liboping.c liboping.h

test_liboping: test_liboping.o liboping.o
