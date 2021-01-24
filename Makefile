TARGETS = image_merge 

CC = gcc
OUTPUT_OPTION=-MMD -MP -o $@
CFLAGS = -c -g -O2 -pthread -fsigned-char -Wall \
         $(shell sdl2-config --cflags) 

SRC_JPEG_MERGE = main.c \
                 util_sdl.c \
                 util_sdl_predefined_displays.c \
                 util_jpeg.c \
                 util_png.c \
                 util_misc.c
OBJ_JPEG_MERGE=$(SRC_JPEG_MERGE:.c=.o)

DEP=$(SRC_JPEG_MERGE:.c=.d)

#
# build rules
#

all: $(TARGETS)

image_merge: $(OBJ_JPEG_MERGE) 
	$(CC) -o $@ $(OBJ_JPEG_MERGE) \
              -pthread -lrt -lm -lpng -ljpeg -lSDL2 -lSDL2_ttf -lSDL2_mixer

-include $(DEP)

#
# clean rule
#

clean:
	rm -f $(TARGETS) $(OBJ_JPEG_MERGE) $(DEP)

