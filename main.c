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
//     -g WxH    : initial width/height of each image, default 320x240
//     -f NAME   : output filename prefix, default 'out'
//     -b COLOR  : select border color
//     -l LAYOUT : 1 = equal size; 2 = first image double size
//     -h        : help
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

#define LAYOUT_EQUAL_SIZE              1
#define LAYOUT_FIRST_IMAGE_DOUBLE_SIZE 2

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

static int32_t layout = LAYOUT_EQUAL_SIZE;

// 
// prototypes
//

static void usage(void);
static void get_combined_image_initial_cols(
    int32_t max_image,   // in
    int32_t * cols);     // out
static void get_combined_image_rows(
    int32_t max_image, int32_t cols,  // in
    int32_t * rows);                  // out
static void get_combined_image_panes(
    int32_t rows, int32_t cols, int32_t win_width, int32_t win_height, int32_t image_width, int32_t image_height, // in
    rect_t * pane, rect_t * pane_full, int32_t * max_pane);                                                       // out
static void get_combined_image_min_cols(
    int32_t * min_cols);  // out

// -----------------  MAIN  ---------------------------------------------------------------------

int main(int argc, char **argv)
{
    char     filename_prefix[100];
    int32_t  win_width, win_height, image_width, image_height, rows, cols;
    rect_t   pane[MAX_IMAGE], pane_full[MAX_IMAGE];
    int32_t  max_pane;
    int32_t  i, border_color, max_texture_dim;
    image_t  image[MAX_IMAGE];
    int32_t  max_image;

    static const border_color_t border_color_tbl[] = {
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
    // initialization
    //

    // initialize
    strcpy(filename_prefix, "out");
    image_width  = 320;
    image_height = 240;
    border_color = GREEN;

    bzero(pane, sizeof(pane));
    bzero(pane_full, sizeof(pane_full));
    max_pane = 0;

    bzero(image, sizeof(image));
    max_image = 0;

    // get options
    while (true) {
        char opt_char = getopt(argc, argv, "g:f:b:l:h");
        if (opt_char == -1) {
            break;
        }
        switch (opt_char) {
        case 'g':
            if (sscanf(optarg, "%dx%d", &image_width, &image_height) != 2) {
                FATAL("invalid '-g %s'\n", optarg);
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
        case 'l':
            if (sscanf(optarg, "%d", &layout) != 1 ||
                layout < 1 || layout > 2) 
            {
                FATAL("invalid '-l %s'\n", optarg);
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

    // determine initial:
    // - cols, rows,
    // - wind_width, win_height
    get_combined_image_initial_cols(max_image, &cols);
    get_combined_image_rows(max_image, cols, &rows);
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

        if (stat(filename, &buf) != 0) {
            ERROR("failed stat of %s, %s\n", filename, strerror(errno));
            continue;
        }

        if (read_png_file(filename, max_texture_dim, &image[i].pixels, &image[i].width, &image[i].height) == 0) {
            INFO("read png file %s\n", filename);
            continue;
        }
        if (read_jpeg_file(filename, max_texture_dim, &image[i].pixels, &image[i].width, &image[i].height) == 0) {
            INFO("read jpeg file %s\n", filename);
            continue;
        }

        ERROR("file %s is not in a supported jpeg or png format\n", filename);
    }

    //
    // runtime loop
    //

    // loop until done
    while (true) {
        // get current window size
        sdl_get_state(&win_width, &win_height, NULL);

        // determine combines image layout values
        get_combined_image_rows(max_image, cols, &rows);
        image_width = (win_width - PANE_BORDER_WIDTH) / cols - PANE_BORDER_WIDTH;
        image_height = (win_height - PANE_BORDER_WIDTH) / rows - PANE_BORDER_WIDTH;
        get_combined_image_panes(rows, cols, win_width, win_height, image_width, image_height,  // in
                                 pane, pane_full, &max_pane);                                   // out

        // sanity check: error if max_pane < max_image
        if (max_pane < max_image) {
            FATAL("max_pane=%d is less than max_image=%d\n", max_pane, max_image);
        }

        // use sdl to draw each of the images to its pane
        sdl_display_init();
        for (i = 0; i < max_pane; i++) {
            sdl_render_pane_border(&pane_full[i], border_color);
            if (image[i].width != 0) {
                texture_t * texture = sdl_create_texture(image[i].width, image[i].height);
                sdl_update_texture(texture, image[i].pixels, image[i].width);
                sdl_render_texture(texture, &pane[i]);
                sdl_destroy_texture(texture);
            }
        }
        sdl_display_present();

        // process sdl events, 
        // stay in this loop until either a display redraw is needed or program terminate
        sdl_event_t * event;
        bool redraw, done;

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
            case '-': case '+': case '=': {   // chagne cols
                int32_t min_cols;
                get_combined_image_min_cols(&min_cols);
                cols += (event->event == '-' ? -1 : 1);
                if (cols < min_cols) {
                    cols = min_cols;
                } else if (cols > MAX_COLS) {
                    cols = MAX_COLS;
                }
                redraw = true;
                break; }
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
    -g WxH    : initial width/height of each image, default 320x240\n\
    -f NAME   : output filename prefix, default 'out'\n\
    -b COLOR  : select border color\n\
    -l LAYOUT : 1 = equal size; 2 = first image double size\n\
    -h        : help\n\
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

// -----------------  SUPPORT MULTIPLE COMBINED IMAGE LAYOUTS  ----------------------------

static void get_combined_image_initial_cols(
    int32_t max_image,   // in
    int32_t * cols)      // out
{
    if (layout == LAYOUT_EQUAL_SIZE) {
        *cols = (max_image == 1 ? 1 :
                 max_image == 2 ? 2 :
                 max_image == 3 ? 3 :
                 max_image == 4 ? 2 
                                : 3);
    } else if (layout == LAYOUT_FIRST_IMAGE_DOUBLE_SIZE) {
        *cols = (max_image == 1 ? 2  
                                : 3);
    } else {
        FATAL("layout %d not supported\n", layout);
    }
}

static void get_combined_image_rows(
    int32_t max_image, int32_t cols,  // in
    int32_t * rows)                   // out
{
    if (layout == LAYOUT_EQUAL_SIZE) {
        *rows = ceil((double)max_image / cols);
    } else if (layout == LAYOUT_FIRST_IMAGE_DOUBLE_SIZE) {
        int32_t images_in_first_2_rows = 1 + 2 * (cols - 2);
        if (images_in_first_2_rows >= max_image) {
            *rows = 2;
        } else {
            *rows = 2 + ceil((double)(max_image - images_in_first_2_rows) / cols);
        }
    } else {
        FATAL("layout %d not supported\n", layout);
    }
}

static void get_combined_image_panes(
    int32_t rows, int32_t cols, int32_t win_width, int32_t win_height, int32_t image_width, int32_t image_height,  // inputs
    rect_t * pane, rect_t * pane_full, int32_t * max_pane)          // outputs
{
    int32_t r, c;

    if (layout == LAYOUT_EQUAL_SIZE) {
        *max_pane = 0;
        for (r = 0; r < rows; r++) {
            for (c = 0; c < cols; c++) {
                sdl_init_pane(&pane_full[*max_pane], &pane[*max_pane],
                              (image_width + PANE_BORDER_WIDTH) * c,
                              (image_height + PANE_BORDER_WIDTH) * r,
                              image_width + 2 * PANE_BORDER_WIDTH, 
                              image_height + 2 * PANE_BORDER_WIDTH);
                (*max_pane)++;
            }
        }
    } else if (layout == LAYOUT_FIRST_IMAGE_DOUBLE_SIZE) {
        *max_pane = 0;

        // first pane is double size
        sdl_init_pane(&pane_full[*max_pane], &pane[*max_pane],
                      0, 0,
                      2 * image_width + 3 * PANE_BORDER_WIDTH, 
                      2 * image_height + 3 * PANE_BORDER_WIDTH);
        (*max_pane)++;

        // init the rest of the panes
        for (r = 0; r < rows; r++) {
            for (c = 0; c < cols; c++) {
                if (r <= 1 && c <= 1) {
                    continue;
                }
                sdl_init_pane(&pane_full[*max_pane], &pane[*max_pane],
                              (image_width + PANE_BORDER_WIDTH) * c,
                              (image_height + PANE_BORDER_WIDTH) * r,
                              image_width + 2 * PANE_BORDER_WIDTH, 
                              image_height + 2 * PANE_BORDER_WIDTH);
                (*max_pane)++;
            }
        }
    } else {
        FATAL("layout %d not supported\n", layout);
    }
}

static void get_combined_image_min_cols(
    int32_t * min_cols)   // out
{
    if (layout == LAYOUT_EQUAL_SIZE) {
        *min_cols = 1;
    } else if (layout == LAYOUT_FIRST_IMAGE_DOUBLE_SIZE) {
        *min_cols = 2;
    } else {
        FATAL("layout %d not supported\n", layout);
    }
}
