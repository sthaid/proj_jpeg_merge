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
//     -i WxH    : initial width/height of each image, default 320x240
//     -o WxH    : initial width/height of output image, no default
//     -c NUM    : initial number of columns, default is
//                 based on number of images and layout
//     -f NAME   : output filename (without extension), default 'out'
//     -l LAYOUT : 1 = equal size; 2 = first image double size, default 1
//     -b COLOR  : select border color, default GREEN
//     -h        : help
//
//     -i and -o can not be combined
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
#include <limits.h>
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
#define MAX_BORDER_COLOR_TBL (sizeof(border_color_tbl) / sizeof(border_color_tbl[0]))

#define LAYOUT_EQUAL_SIZE              1
#define LAYOUT_FIRST_IMAGE_DOUBLE_SIZE 2

#define DEFAULT_IMAGE_WIDTH  320
#define DEFAULT_IMAGE_HEIGHT 240

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
static void layout_init(
    int32_t max_image, int32_t image_width, int32_t image_height,     // in
    int32_t * win_width, int32_t * win_height, int32_t * cols,        // in out
    int32_t * min_cols, int32_t * max_cols);                          // out
static void layout_get_panes(
    int32_t max_image, int32_t win_width, int32_t win_height, int32_t cols,    // in
    rect_t * pane, rect_t * pane_full, int32_t * max_pane,                     // out
    int32_t * win_width_used, int32_t * win_height_used);

// -----------------  MAIN  ---------------------------------------------------------------------

int main(int argc, char **argv)
{
    static int32_t  win_width, win_height, image_width, image_height, cols, min_cols, max_cols;
    static rect_t   pane[MAX_IMAGE], pane_full[MAX_IMAGE];
    static int32_t  max_pane;
    static image_t  image[MAX_IMAGE];
    static int32_t  max_image;
    static char     filename_out[PATH_MAX];
    static int32_t  border_color;
    static int32_t  max_texture_dim;
    static int32_t  i;

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

    // initialize non zero variables
    strcpy(filename_out, "out");
    border_color = GREEN;

    // get options
    while (true) {
        char opt_char = getopt(argc, argv, "i:o:c:f:l:b:h");
        if (opt_char == -1) {
            break;
        }
        switch (opt_char) {
        case 'i':
            if (sscanf(optarg, "%dx%d", &image_width, &image_height) != 2 ||
                image_width <= 0 || image_height <= 0) 
            {
                FATAL("invalid '-i %s'\n", optarg);
            }
            break;
        case 'o':
            if (sscanf(optarg, "%dx%d", &win_width, &win_height) != 2 ||
                win_width <= 0 || win_height <= 0) 
            {
                FATAL("invalid '-o %s'\n", optarg);
            }
            break;
        case 'c': 
            if (sscanf(optarg, "%d", &cols) != 1 || cols <= 0) {
                FATAL("invalid '-c %s'\n", optarg);
            }
            break;
        case 'f':
            strcpy(filename_out, optarg);
            break;
        case 'l':
            if ((sscanf(optarg, "%d", &layout) != 1) ||
                (layout != LAYOUT_EQUAL_SIZE && 
                 layout != LAYOUT_FIRST_IMAGE_DOUBLE_SIZE))
            {
                FATAL("invalid '-l %s'\n", optarg);
            }
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

    // if both image and window dims supplied then error
    if (win_width != 0 && image_width != 0) {
        FATAL("-o and -i options can not be combined\n");
    }

    // determine max_image, and 
    // verify at leat 1 image supplied
    max_image = argc - optind;
    if (max_image == 0) {
        usage();
        exit(1);
    }

    // layout init
    layout_init(max_image, image_width, image_height,  // in
                &win_width, &win_height, &cols,        // in out
                &min_cols, &max_cols);                 // out

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
        sdl_event_t * event;
        bool          redraw=false, done=false;
        int32_t       win_width_used, win_height_used;

        // get current window size
        sdl_get_state(&win_width, &win_height, NULL);

        // layout get panes
        layout_get_panes(max_image, win_width, win_height, cols,   // in
                         pane, pane_full, &max_pane,               // out
                         &win_width_used, &win_height_used);

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

        // process sdl events
        sdl_event_register('w', SDL_EVENT_TYPE_KEY, NULL);   // write png file
        sdl_event_register('q', SDL_EVENT_TYPE_KEY, NULL);   // quit program
        sdl_event_register('-', SDL_EVENT_TYPE_KEY, NULL);   // decrease cols
        sdl_event_register('+', SDL_EVENT_TYPE_KEY, NULL);   // increase cols
        sdl_event_register('=', SDL_EVENT_TYPE_KEY, NULL);   // increase cols
        while (true) {
            // get and process the event
            event = sdl_poll_event();
            switch (event->event) {
            case SDL_EVENT_QUIT: case 'q':    // quit
                done = true;
                break;
            case 'w': {                       // write the png file
                char filename[PATH_MAX];
                rect_t rect = {0, 0, win_width_used, win_height_used};
                sprintf(filename, "%s.png", filename_out);
                INFO("writing %s, width=%d height=%d\n", filename_out, win_width_used, win_height_used); 
                sdl_print_screen(filename, true, &rect);
                redraw = true;
                break; }
            case '-': case '+': case '=': {   // chagne cols
                cols += (event->event == '-' ? -1 : 1);
                if (cols < min_cols) {
                    cols = min_cols;
                } else if (cols > max_cols) {
                    cols = max_cols;
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

            // if time to terminate program or redraw display then 
            // exit the 'while (true)' loop
            if (done || redraw) {
                break;
            }

            // delay 1 ms
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
    -i WxH    : initial width/height of each image, default 320x240\n\
    -o WxH    : initial width/height of output image, no default\n\
    -c NUM    : initial number of columns, default is\n\
                based on number of images and layout\n\
    -f NAME   : output filename (without extension), default 'out'\n\
    -l LAYOUT : 1 = equal size; 2 = first image double size, default 1\n\
    -b COLOR  : select border color, default GREEN\n\
    -h        : help\n\
\n\
    -i and -o can not be combined\n\
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

// -----------------  MULTIPLE LAYOUT SUPPORT  --------------------------------------------

static void layout_init(
    int32_t max_image, int32_t image_width, int32_t image_height,     // in
    int32_t * win_width, int32_t * win_height, int32_t * cols,        // in out
    int32_t * min_cols, int32_t * max_cols)                           // out
{
    int32_t rows;

    // determine valid cols range
    if (layout == LAYOUT_EQUAL_SIZE) {
        *min_cols = 1;
        *max_cols = 10;
    } else if (layout == LAYOUT_FIRST_IMAGE_DOUBLE_SIZE) {
        *min_cols = 2;
        *max_cols = 10;
    } else {
        FATAL("layout %d not supported\n", layout);
    }

    // determine cols ...
    // if cols arg supplied then
    //    verify supplied cols is in range
    // else
    //    get_out_image_recommended_cols
    // endif
    if (*cols > 0) {
        if (*cols < *min_cols || *cols > *max_cols) {
            FATAL("cols %d not in ragne %d - %d\n", *cols, *min_cols, *max_cols);
        }
    } else {
        if (layout == LAYOUT_EQUAL_SIZE) {
            *cols = (max_image == 1 ? 1 :
                     max_image == 2 ? 2 :
                     max_image == 3 ? 3 :
                     max_image == 4 ? 2 
                                    : 3);
        } else { // LAYOUT_FIRST_IMAGE_DOUBLE_SIZE
            *cols = (max_image == 1 ? 2  
                                    : 3);
        }
    }

    // determine rows;  rows is just used local to this routine
    if (layout == LAYOUT_EQUAL_SIZE) {
        rows = ceil((double)max_image / (*cols));
    } else { // LAYOUT_FIRST_IMAGE_DOUBLE_SIZE
        int32_t images_in_first_2_rows = 1 + 2 * ((*cols) - 2);
        if (images_in_first_2_rows >= max_image) {
            rows = 2;
        } else {
            rows = 2 + ceil((double)(max_image - images_in_first_2_rows) / (*cols));
        }
    }

    // determine win_width, win_height ...
    // if win dims not supplied
    //    if image dims not supplied
    //        use default image dims
    //    endif
    //    determine win dimensions from image dimensions
    // endif
    if (*win_width == 0) {
        if (image_width == 0) {
            image_width  = DEFAULT_IMAGE_WIDTH;
            image_height = DEFAULT_IMAGE_HEIGHT;
        }
        *win_width = (image_width + PANE_BORDER_WIDTH) * (*cols) + PANE_BORDER_WIDTH;
        *win_height = (image_height + PANE_BORDER_WIDTH) * rows + PANE_BORDER_WIDTH;
    }
}

static void layout_get_panes(
    int32_t max_image, int32_t win_width, int32_t win_height, int32_t cols,    // in
    rect_t * pane, rect_t * pane_full, int32_t * max_pane,                     // out
    int32_t * win_width_used, int32_t * win_height_used)
{
    int32_t r, c; 
    int32_t rows;
    int32_t image_width, image_height;

    // determine rows;  rows is just used local to this routine
    if (layout == LAYOUT_EQUAL_SIZE) {
        rows = ceil((double)max_image / cols);
    } else { // LAYOUT_FIRST_IMAGE_DOUBLE_SIZE
        int32_t images_in_first_2_rows = 1 + 2 * (cols - 2);
        if (images_in_first_2_rows >= max_image) {
            rows = 2;
        } else {
            rows = 2 + ceil((double)(max_image - images_in_first_2_rows) / cols);
        }
    }

    // determine image_width and image_height;
    // these are also just used local to this routine
    image_width = (win_width - PANE_BORDER_WIDTH) / cols - PANE_BORDER_WIDTH;
    image_height = (win_height - PANE_BORDER_WIDTH) / rows - PANE_BORDER_WIDTH;

    // determine pane, pane_full, and max_pane
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
    } else { // LAYOUT_FIRST_IMAGE_DOUBLE_SIZE
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
    }

    // determine win_width_used and win_height_used, these
    // may be slightly less than win_width/height
    *win_width_used = (image_width + PANE_BORDER_WIDTH) * cols + PANE_BORDER_WIDTH;
    *win_height_used = (image_height + PANE_BORDER_WIDTH) * rows + PANE_BORDER_WIDTH;
}

