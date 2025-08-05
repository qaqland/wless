TARGET = clear.out

SDL3 = $(shell pkg-config --cflags --libs sdl3)
#CFLAGS = -Wall -Wextra -Werror

all: $(TARGET)

$(TARGET): clear.c
	cc -o $@ $< $(SDL3) $(CFLAGS)
