CC=gcc
CFLAGS=-Wall -Iincludes -Wextra -std=gnu99 -ggdb $(shell pkg-config --cflags gstreamer-1.0)
LDLIBS=-lqrencode -lgdk_pixbuf-2.0 $(shell pkg-config --libs gstreamer-1.0)
VPATH=src

all: tunnel test

tunnel: tunnel.c tunnel_start_server.c tunnel_end_server.c args.c common.c window.c eventqueue.c

test: test.c window.c

clean:
	rm -rf *~ *.o tunnel test

.PHONY : clean all
