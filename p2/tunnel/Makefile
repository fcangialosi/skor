CC=gcc
CFLAGS=-Wall -Iincludes -Wextra -std=gnu99 -ggdb $(shell pkg-config --cflags gstreamer-1.0)
LDLIBS=-lqrencode -lgdk_pixbuf-2.0 $(shell pkg-config --libs gstreamer-1.0)
VPATH=src

all: tunnel

tunnel: tunnel.c tunnel_start_server.c args.c

clean:
	rm -rf *~ *.o tunnel

.PHONY : clean all
