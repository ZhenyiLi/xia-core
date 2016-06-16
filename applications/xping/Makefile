include ../../xia.mk

CC=g++
LDFLAGS += $(LIBS)

OUT=xping
TARGETS=$(addprefix $(BINDIR)/, $(OUT))
SOURCES=$(addsuffix .c, $(OUT))

all: xping

xping:
	$(CC) $(CFLAGS) xping.c -o xping $(LDFLAGS)

clean: 
	-rm $(TARGETS)
