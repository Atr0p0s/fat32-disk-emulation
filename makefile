SOURCES = 	main.c \
			commands.c \
			fat32impl.c \
			tools.c

INCLUDES =	commands.h \
			fat32impl.h \
			tools.h

CFLAGS =	

CC = gcc

BASENAME := f32disk
TARGET := $(BASENAME)
OBJS := $(SOURCES:.c=.o)
DEPS := $(OBJS:.o=.d)

-include $(DEPS)

%.o: %.c
	$(CC) -c $< $(CFLAGS) -o $@
	$(CC) -MM $(CFLAGS) $*.c > $*.d

.PHONY: build
build:$(TARGET)
$(TARGET): $(OBJS)
	$(CC) $(OBJS) $(CFLAGS) -o $@

.PHONY: clean
clean:
	rm -f $(OBJS) $(TARGET) *.d

.PHONY: compile-all
compile-all: $(OBJS)