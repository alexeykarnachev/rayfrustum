CC = gcc
CFLAGS = -Wall -std=c99
LDFLAGS = -L../deps/lib
LIBS = -lraylib -lm -lpthread -ldl
INCLUDES = -I../deps/include -I../include


rayfrustum: rayfrustum.c ../deps/include/raygizmo.h
	$(CC) $(CFLAGS) $(INCLUDES) -o rayfrustum rayfrustum.c $(LDFLAGS) $(LIBS)

