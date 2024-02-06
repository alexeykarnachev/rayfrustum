CC = gcc
CFLAGS = -Wall -std=c99
LDFLAGS = -L../deps/lib
LIBS = -lraylib -lm -lpthread -ldl
INCLUDES = -I../deps/include -I../include


rayfrustum: rayfrustum.c
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ $^ $(LDFLAGS) $(LIBS)

