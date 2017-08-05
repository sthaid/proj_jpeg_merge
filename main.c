/*
Copyright (c) 2017 Steven Haid

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

//
// SYNOPSIS: 
//     image_merge [OPTIONS] [JPEG_OR_PNG_FILES...]
// 
// DESCRIPTION
//     This program reads jpeg and png files and combines them into a
//     single png output file.
// 
// OPTIONS
//     -h       : help
//     -g WxH   : initial width/height of each image, default 320x240
//     -c N     : initial number of image columns, 
//                default varies based on number of images
//     -f NAME  : output filename prefix, default 'out'
// 
// RUN TIME CONTROLS
//     Keyboard Controls:
//         q  : exit program
//         w  : write the png file
//         -  : decrease number of columns
//         +  : increase number of columns
//     The window can also be resized using the mouse.
//

//
// XXX Possible Future Enhancements
// - add option to set the border color
// - support layout with double size image at top/left
// - add option to output in jpeg format
//

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <math.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "util_sdl.h"
#include "util_jpeg.h"
#include "util_png.h"
#include "util_misc.h"

// 
// defines
//

#define MAX_IMAGE 1000
#define MAX_COLS  10
#define MAX_BORDER_COLOR_TBL (sizeof(border_color_tbl) / sizeof(border_color_tbl[0]))

//
// typedefs
//

typedef struct {
    uint8_t * pixels;
    int32_t   width;
    int32_t   height;
} image_t;

typedef struct {
    char * name;
    int32_t color;
} border_color_t;

// 
// variables
//

int32_t  image_width  = 320;
int32_t  image_height = 240;
char     filename_prefix[100] = "out";
int32_t  border_color = GREEN;
int32_t  cols, rows;
int32_t  win_width, win_height;
image_t  image[MAX_IMAGE];
int32_t  max_image;
int32_t  max_texture_dim;

border_color_t border_color_tbl[] = {
    { "PURPLE",     PURPLE     },
    { "BLUE",       BLUE       },
    { "LIGHT_BLUE", LIGHT_BLUE },
    { "GREEN",      GREEN      },
    { "YELLOW",     YELLOW     },
    { "ORANGE",     ORANGE     },
    { "PINK",       PINK       },
    { "RED",        RED        },
    { "GRAY",       GRAY       },
    { "WHITE",      WHITE      },
    { "BLACK",      BLACK      },  };

// 
// prototypes
//

static void usage(void);

// -----------------  MAIN  ---------------------------------------------------------------------

int main(int argc, char **argv)
{
    int32_t i, ret;

    // get options
    while (true) {
        char opt_char = getopt(argc, argv, "g:c:f:b:h");
        if (opt_char == -1) {
            break;
        }
        switch (opt_char) {
        case 'g':
            if (sscanf(optarg, "%dx%d", &image_width, &image_height) != 2) {
                FATAL("invalid '-g %s'\n", optarg);
            }
            break;
        case 'c':
            if (sscanf(optarg, "%d", &cols) != 1 ||
                cols < 1 || cols > MAX_COLS) 
            {
                FATAL("invalid '-c %s'\n", optarg);
            }
            break;
        case 'f':
            strcpy(filename_prefix, optarg);
            break;
        case 'b':
            for (i = 0; i < MAX_BORDER_COLOR_TBL; i++) {
                if (strcasecmp(border_color_tbl[i].name, optarg) == 0) {
                    border_color = border_color_tbl[i].color;
                    break;
                }
            }
            if (i == MAX_BORDER_COLOR_TBL) {
                FATAL("invalid '-b %s'\n", optarg);
            }
            break;
        case 'h':
            usage();
            exit(0);
        default:
            break;
        }
    }

    // determine number of image filenames supplies, and 
    // verify at least one has been supplied
    max_image = argc - optind;
    if (max_image == 0) {
        usage();
        exit(1);
    }

    // determine:
    // - cols, rows,
    // - wind_width, win_height
    if (cols == 0) {
        cols = (max_image == 1 ? 1 :
                max_image == 2 ? 2 :
                max_image == 3 ? 3 :
                max_image == 4 ? 2 :
                max_image == 5 ? 3 :
                max_image == 6 ? 3 :
                max_image == 7 ? 4 :
                max_image == 8 ? 4 :
                max_image == 9 ? 3 :
                                 4);
    }
    rows = ceil((double)max_image / cols);
    if (rows == 0) {
        rows = 1;
    }
    win_width = (image_width + PANE_BORDER_WIDTH) * cols + PANE_BORDER_WIDTH;
    win_height = (image_height + PANE_BORDER_WIDTH) * rows + PANE_BORDER_WIDTH;

    // sdl init
    if (sdl_init(win_width, win_height, NULL, &max_texture_dim) < 0) {
        FATAL("sdl_init %dx%d failed\n", win_width, win_height);
    }

    // read all jpeg / png files
    for (i = 0; i < max_image; i++) {
        char * filename = argv[optind+i];
        struct stat buf;

        ret = stat(filename, &buf);
        if (ret != 0) {
            ERROR("failed stat of %s, %s\n", filename, strerror(errno));
            continue;
        }

        ret = read_png_file(filename, max_texture_dim,
                            &image[i].pixels, &image[i].width, &image[i].height);
        if (ret == 0) {
            continue;
        }

        ret = read_jpeg_file(filename, max_texture_dim,
                             &image[i].pixels, &image[i].width, &image[i].height);
        if (ret == 0) {
            continue;
        }

        ERROR("file %s is not in a supported jpeg or png format\n", filename);
    }

    // loop until done
    while (true) {
        rect_t pane[MAX_IMAGE];
        rect_t pane_full[MAX_IMAGE];
        texture_t * texture;
        sdl_event_t * event;
        bool redraw, done;

        // get current window size
        sdl_get_state(&win_width, &win_height, NULL);

        // update rows based on possible new value of cols
        rows = ceil((double)max_image / cols);
        if (rows == 0) {
            rows = 1;
        }

        // update image_width/height based on possible new values of 
        // win_width/height or rows or cols
        image_width = (win_width - PANE_BORDER_WIDTH) / cols - PANE_BORDER_WIDTH;
        image_height = (win_height - PANE_BORDER_WIDTH) / rows - PANE_BORDER_WIDTH;

        // init panes for each row/col position
        for (i = 0; i < rows*cols; i++) {
            sdl_init_pane(&pane_full[i], &pane[i],
                          (image_width + PANE_BORDER_WIDTH) * (i % cols),
                          (image_height + PANE_BORDER_WIDTH) * (i / cols),
                          image_width + 2 * PANE_BORDER_WIDTH, 
                          image_height + 2 * PANE_BORDER_WIDTH);
        }

        // use sdl to draw each of the images to its pane
        sdl_display_init();
        for (i = 0; i < rows*cols; i++) {
            sdl_render_pane_border(&pane_full[i], border_color);
            if (i >= max_image || image[i].width == 0) {
                continue;
            }

            texture = sdl_create_texture(image[i].width, image[i].height);
            sdl_update_texture(texture, image[i].pixels, image[i].width);
            sdl_render_texture(texture, &pane[i]);
            sdl_destroy_texture(texture);
        }
        sdl_display_present();

        // process sdl events, 
        // stay in this loop until either a display redraw is needed or program terminate
        sdl_event_register('w', SDL_EVENT_TYPE_KEY, NULL);   // write png file
        sdl_event_register('q', SDL_EVENT_TYPE_KEY, NULL);   // quit program
        sdl_event_register('-', SDL_EVENT_TYPE_KEY, NULL);   // decrease cols
        sdl_event_register('+', SDL_EVENT_TYPE_KEY, NULL);   // increase cols
        sdl_event_register('=', SDL_EVENT_TYPE_KEY, NULL);   // increase cols
        redraw = false;
        done = false;
        while (true) {
            event = sdl_poll_event();
            switch (event->event) {
            case SDL_EVENT_QUIT: case 'q':    // quit
                done = true;
                break;
            case 'w': {                       // write the png file
                char filename[100];
                sprintf(filename, "%s_%d_%d.png", filename_prefix, win_width, win_height);
                INFO("writing %s\n", filename);
                sdl_print_screen(filename, true);
                redraw = true;
                break; }
            case '-': case '+': case '=':     // chagne cols
                cols += (event->event == '-' ? -1 : 1);
                if (cols < 1) cols = 1;
                if (cols > MAX_COLS) cols = MAX_COLS;
                redraw = true;
                break;
            case SDL_EVENT_WIN_SIZE_CHANGE:   // window size has changed
            case SDL_EVENT_WIN_RESTORED:      // window has been unminized
            case SDL_EVENT_SCREENSHOT_TAKEN:  // a screen shot was taken
                redraw = true;
                break;
            default:                          // ignore any other events
                break;
            }

            if (done || redraw) {
                break;
            }

            usleep(1000);
        }

        // check if time to exit program
        if (done) {
            break;
        }
    }

    return 0;
}

static void usage(void) 
{
    printf("\
SYNOPSIS: \n\
    image_merge [OPTIONS] [JPEG_OR_PNG_FILES...]\n\
\n\
DESCRIPTION\n\
    This program reads jpeg and png files and combines them into a\n\
    single png output file.\n\
\n\
OPTIONS\n\
    -h       : help\n\
    -g WxH   : initial width/height of each image, default 320x240\n\
    -c N     : initial number of image columns, \n\
               default varies based on number of images\n\
    -f NAME  : output filename prefix, default 'out'\n\
\n\
RUN TIME CONTROLS\n\
    Keyboard Controls:\n\
        q  : exit program\n\
        w  : write the png file\n\
        -  : decrease number of columns\n\
        +  : increase number of columns\n\
    The window can also be resized using the mouse.\n\
");
}
