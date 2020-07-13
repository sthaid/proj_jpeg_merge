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
//     single jpeg or png output file. Each of the images can optionally be cropped.
// 
// OPTIONS
//     -i WxH      : initial width/height of each image, default 320x240 
//     -i W        : initial width of each image, the image height will be
//                   set using 1.333 aspect ratio
//     -o WxH      : initial width/height of the combined output
//     -o W        : initial width of the combined ouput, the combined output 
//                   height will be set based on the number of rows and cols
//                   and 1.333 aspect ratio 
//     -c NUM      : initial number of columns, default is
//                   based on number of images and layout
//     -f NAME     : output filename, must have .jpg or .png extension,
//                   default 'out.jpg'
//     -l LAYOUT   : 1 = equal size; 2 = first image double size, default 1
//     -b COLOR    : select border color, default GREEN, choices are 
//                   NONE, PURPLE, BLUE, LIGHT_BLUE, GREEN, YELLOW, ORANGE, 
//                   PINK, RED, GRAY, WHITE, BLACK 
//     -k n,x,y,w,h: crop image n; x,y,w,h are in percent; x,y are the upper left of
//                   the crop area; w,h are the size of the crop area
//     -z          : enable batch mode, the combined output will be written and
//                   this program terminates
//     -h          : help
//
//     -i and -o can not be combined
// 
// RUN TIME CONTROLS - WHEN NOT IN BATCH MODE
//     General Keyboard Controls
//         w      write file containing the combined images
//         q      exit the program
//         c, C   decrease or increase the number of image columns
//
//     Window Resize Control
//         mouse
//
//     Crop Image Keyboard Controls
//         Tab, ShiftTab     select an image to be cropped
//         arrow keys        adjust the position of the crop area
//         shift arrow keys  adjust the aspect ratio of the crop area
//         -, +, =           adjust the size of the crop area (= is same as +)
//         Enter             apply the crop
//         Esc               exit crop mode without applying the crop
//         r                 reset the selected image to it's original size
//         R                 reset all images to their original size
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

#define NO_BORDER -1

#define DEFAULT_ASPECT_RATIO   1.333333

#define CROP_STEP 0.5

//
// typedefs
//

typedef struct {
    double x, y, w, h;
} crop_t;

typedef struct {
    char    * filename;
    uint8_t * pixels;
    int32_t   width;
    int32_t   height;
    crop_t    crop;
} image_t;

typedef struct {
    char * name;
    int32_t color;
} border_color_t;

// 
// variables
//

static int32_t   max_image;
static image_t   image[MAX_IMAGE];
static rect_t    pane[MAX_IMAGE], pane_full[MAX_IMAGE];
static texture_t cached_texture[MAX_IMAGE];

static int32_t   max_pane;

static bool      crop_enabled;
static int32_t   crop_idx;
static crop_t    crop; 
static crop_t    crop_uncropped;

static int32_t   layout = LAYOUT_EQUAL_SIZE;

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
static int32_t  border_color;
static char   * border_color_str;

// 
// prototypes
//

static void usage(void);
void draw_images(void);
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
    static int32_t  win_width, win_height;
    static int32_t  image_width, image_height;
    static int32_t  cols, min_cols, max_cols;
    static char     output_filename[PATH_MAX];
    static bool     batch_mode;
    static int32_t  max_texture_dim;
    static bool     done;
    static bool     print_screen_request;
    static int32_t  i;

    // 
    // initialization
    //

    // initialize non zero variables
    strcpy(output_filename, "out.jpg");
    border_color = GREEN;
    border_color_str = "GREEN";
    crop_uncropped.w = crop_uncropped.h = 100;
    for (i = 0; i < MAX_IMAGE; i++) {
        image[i].crop = crop_uncropped;
    }

    // get options
    while (true) {
        char opt_char = getopt(argc, argv, "i:o:c:f:l:b:k:zh");
        if (opt_char == -1) {
            break;
        }
        switch (opt_char) {
        case 'i':
            if (sscanf(optarg, "%dx%d", &image_width, &image_height) == 2) {
                if (image_width <= 0 || image_height <= 0) {
                    FATAL("invalid '-i %s'\n", optarg);
                }
            } else if (sscanf(optarg, "%d", &image_width) == 1) {
                if (image_width <= 0) {
                    FATAL("invalid '-i %s'\n", optarg);
                }
            } else {
                FATAL("invalid '-i %s'\n", optarg);
            }
            break;
        case 'o':
            if (sscanf(optarg, "%dx%d", &win_width, &win_height) == 2) {
                if (win_width <= 0 || win_height <= 0) {
                    FATAL("invalid '-o %s'\n", optarg);
                }
            } else if (sscanf(optarg, "%d", &win_width) == 1) {
                if (win_width <= 0) {
                    FATAL("invalid '-o %s'\n", optarg);
                }
            } else {
                FATAL("invalid '-o %s'\n", optarg);
            }
            break;
        case 'c': 
            if (sscanf(optarg, "%d", &cols) != 1 || cols <= 0) {
                FATAL("invalid '-c %s'\n", optarg);
            }
            break;
        case 'f': {
            size_t len;
            strcpy(output_filename, optarg);
            len = strlen(output_filename);
            if ((len < 5) || 
                (strcmp(output_filename+len-4, ".png") != 0 &&
                 strcmp(output_filename+len-4, ".jpg") != 0))
            {
                FATAL("invalid '-f %s'\n", optarg);
            }
            break; }
        case 'l':
            if ((sscanf(optarg, "%d", &layout) != 1) ||
                (layout != LAYOUT_EQUAL_SIZE && 
                 layout != LAYOUT_FIRST_IMAGE_DOUBLE_SIZE))
            {
                FATAL("invalid '-l %s'\n", optarg);
            }
            break;
        case 'b':
            if (strcasecmp(optarg, "NONE") == 0) {
                border_color = NO_BORDER;
                break;
            }
            for (i = 0; i < MAX_BORDER_COLOR_TBL; i++) {
                if (strcasecmp(border_color_tbl[i].name, optarg) == 0) {
                    border_color = border_color_tbl[i].color;
                    break;
                }
            }
            if (i == MAX_BORDER_COLOR_TBL) {
                FATAL("invalid '-b %s'\n", optarg);
            }
            border_color_str = optarg;
            break;
        case 'k': {
            int32_t image_idx;
            crop_t  crop;
            if (sscanf(optarg, "%d,%lf,%lf,%lf,%lf", &image_idx, &crop.x, &crop.y, &crop.w, &crop.h) != 5) {
                FATAL("invalid '-k %s'\n", optarg);
            }
            if (image_idx < 0 || image_idx >= MAX_IMAGE ||
                crop.x < 0 || crop.y < 0 || crop.w < 5 || crop.h < 5 ||
                crop.x + crop.w > 100 || crop.y + crop.h > 100) 
            {
                FATAL("invalid '-k %s'\n", optarg);
            }
            image[image_idx].crop = crop;
            break; }
        case 'z':
            batch_mode = true;
            break;
        case 'h':
            usage();
            exit(0);
        default:
            exit(1);
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

    // read all jpeg / png image files
    for (i = 0; i < max_image; i++) {
        char * filename = argv[optind+i];
        struct stat buf;

        image[i].filename = filename;

        if (stat(filename, &buf) != 0) {
            ERROR("failed stat of %s, %s\n", filename, strerror(errno));
            continue;
        }

        if (read_png_file(filename, max_texture_dim, &image[i].pixels, &image[i].width, &image[i].height) == 0) {
            INFO("read png file %s  %dx%d\n", filename, image[i].width, image[i].height);
            continue;
        }
        if (read_jpeg_file(filename, max_texture_dim, &image[i].pixels, &image[i].width, &image[i].height) == 0) {
            INFO("read jpeg file %s  %dx%d\n", filename, image[i].width, image[i].height);
            continue;
        }

        ERROR("file %s is not in a supported jpeg or png format\n", filename);
    }


    //
    // runtime loop
    //

    // loop until done
    while (true) {
        int32_t win_width_used, win_height_used;
        static sdl_event_t * event;

        // get current window size
        sdl_get_state(&win_width, &win_height, NULL);

        // sanitize crop
        if (crop.x < 0) crop.x = 0;
        if (crop.x > 98) crop.x = 98;
        if (crop.x + crop.w >= 99.9999) crop.w = 99.9999 - crop.x;

        if (crop.y < 0) crop.y = 0;
        if (crop.y > 98) crop.y = 98;
        if (crop.y + crop.h >= 99.9999) crop.h = 99.9999 - crop.y;

        // get pane locations for the current layout and window dims
        layout_get_panes(max_image, win_width, win_height, cols,   // in
                         pane, pane_full, &max_pane,               // out
                         &win_width_used, &win_height_used);

        // sanity check: error if max_pane < max_image
        if (max_pane < max_image) {
            FATAL("max_pane=%d is less than max_image=%d\n", max_pane, max_image);
        }

        // use sdl to draw each of the images to its pane
        // XXX on some computers the draw_images needs to be done
        //     twice when creating the output file; I don't know why
        draw_images();
        if (print_screen_request || batch_mode) {
            draw_images();
        }

        // if need to create the output_file, because either
        // processing the 'w' event, or in batch mode then ...
        if (print_screen_request || batch_mode) {
            char cmd_str[10000];
            char *p = cmd_str;

            // debug print the name and size of the combined output file being created
            INFO("writing %s, width=%d height=%d\n", output_filename, win_width_used, win_height_used); 

            // debug print the bach command that can be used to recreate
            p += sprintf(p, "image_merge -o %dx%d -c %d -f %s -l %d -b %s -z ",
                    win_width_used, win_height_used, cols, output_filename, layout, border_color_str);
            for (i = 0; i < max_image; i++) {
                if (memcmp(&image[i].crop, &crop_uncropped, sizeof(crop_t)) != 0) {
                    p += sprintf(p, "-k %d,%g,%g,%g,%g ",
                                i, image[i].crop.x, image[i].crop.y, image[i].crop.w, image[i].crop.h);
                }
            }
            for (i = 0; i < max_image; i++) {
                p += sprintf(p, "%s ", image[i].filename);
            }
            INFO("%s\n", cmd_str);

            // if in batch_mode then delay 1 second so user can briefly see 
            // what the output_filename will look like
            if (batch_mode) {
                sleep(1);
            }

            // create the output_filename, 
            // when invoked with print_screen_request then flash the screen
            rect_t rect = {0, 0, win_width_used, win_height_used};
            sdl_print_screen(output_filename, print_screen_request, &rect);

            // if in batch_mode then exit the program, else continue so the screen is redrawn
            if (batch_mode) {
                exit(0);
            } else {
                print_screen_request = false;
                continue;
            }
        }

        // register for events
        sdl_event_register('w',                             SDL_EVENT_TYPE_KEY, NULL);  // write out file
        sdl_event_register('q',                             SDL_EVENT_TYPE_KEY, NULL);  // quit program
        sdl_event_register('c',                             SDL_EVENT_TYPE_KEY, NULL);  // adjust cols   
        sdl_event_register('C',                             SDL_EVENT_TYPE_KEY, NULL);  
        sdl_event_register(SDL_EVENT_KEY_TAB,               SDL_EVENT_TYPE_KEY, NULL);  // crop support
        sdl_event_register(SDL_EVENT_KEY_SHIFT_TAB,         SDL_EVENT_TYPE_KEY, NULL);
        sdl_event_register(SDL_EVENT_KEY_UP_ARROW,          SDL_EVENT_TYPE_KEY, NULL);
        sdl_event_register(SDL_EVENT_KEY_DOWN_ARROW,        SDL_EVENT_TYPE_KEY, NULL);
        sdl_event_register(SDL_EVENT_KEY_LEFT_ARROW,        SDL_EVENT_TYPE_KEY, NULL);
        sdl_event_register(SDL_EVENT_KEY_RIGHT_ARROW,       SDL_EVENT_TYPE_KEY, NULL);
        sdl_event_register(SDL_EVENT_KEY_SHIFT_UP_ARROW,    SDL_EVENT_TYPE_KEY, NULL);
        sdl_event_register(SDL_EVENT_KEY_SHIFT_DOWN_ARROW,  SDL_EVENT_TYPE_KEY, NULL);
        sdl_event_register(SDL_EVENT_KEY_SHIFT_LEFT_ARROW,  SDL_EVENT_TYPE_KEY, NULL);
        sdl_event_register(SDL_EVENT_KEY_SHIFT_RIGHT_ARROW, SDL_EVENT_TYPE_KEY, NULL);
        sdl_event_register('-',                             SDL_EVENT_TYPE_KEY, NULL);
        sdl_event_register('+',                             SDL_EVENT_TYPE_KEY, NULL);  
        sdl_event_register('=',                             SDL_EVENT_TYPE_KEY, NULL); 
        sdl_event_register(SDL_EVENT_KEY_ENTER,             SDL_EVENT_TYPE_KEY, NULL);
        sdl_event_register(SDL_EVENT_KEY_ESC,               SDL_EVENT_TYPE_KEY, NULL);
        sdl_event_register('r',                             SDL_EVENT_TYPE_KEY, NULL);
        sdl_event_register('R',                             SDL_EVENT_TYPE_KEY, NULL);

        // process events
        while (true) {
            bool unsupported_event = false;

            // get and process the event
            event = sdl_poll_event();
            switch (event->event) {

            // quit program
            case SDL_EVENT_QUIT: case 'q':
                sdl_play_event_sound();
                done = true;
                break;

            // write jpg or png file, depending on output_filename extension
            case 'w': {
                sdl_play_event_sound();
                print_screen_request = true;
                crop_enabled  = false;
                break; }

            // chagne cols
            case 'c': case 'C':
                if ((cols == min_cols && event->event == 'c') || 
                    (cols == max_cols && event->event == 'C'))
                {
                    break;
                }
                sdl_play_event_sound();
                cols += (event->event == 'c' ? -1 : 1);
                for (i = 0; i < MAX_IMAGE; i++) {
                    sdl_destroy_texture(cached_texture[i]);
                    cached_texture[i] = NULL;
                }
                break;

            // crop events follow ...
            case SDL_EVENT_KEY_TAB: case SDL_EVENT_KEY_SHIFT_TAB: 
                sdl_play_event_sound();
                if (crop_enabled) {
                    if (event->event == SDL_EVENT_KEY_TAB) {
                        crop_idx = (crop_idx == max_image - 1 ? 0 : crop_idx + 1);
                    } else {
                        crop_idx = (crop_idx == 0 ? max_image - 1 : crop_idx - 1);
                    }
                }
                crop.w = 50;
                crop.h = 50;
                crop.x = 50 - crop.w / 2;
                crop.y = 50 - crop.h / 2;
                crop_enabled = true;
                break;
            case SDL_EVENT_KEY_UP_ARROW:
                if (!crop_enabled) {
                    break;
                }
                if (crop.y > 0) {
                    crop.y -= CROP_STEP;
                }
                break;
            case SDL_EVENT_KEY_DOWN_ARROW:
                if (!crop_enabled) {
                    break;
                }
                if (crop.y + crop.h < 100) {
                    crop.y += CROP_STEP;
                }
                break;
            case SDL_EVENT_KEY_LEFT_ARROW:
                if (!crop_enabled) {
                    break;
                }
                if (crop.x > 0) {
                    crop.x -= CROP_STEP;
                }
                break;
            case SDL_EVENT_KEY_RIGHT_ARROW:
                if (!crop_enabled) {
                    break;
                }
                if (crop.x + crop.w < 100) {
                    crop.x += CROP_STEP;
                }
                break;
            case SDL_EVENT_KEY_SHIFT_DOWN_ARROW:
                if (!crop_enabled) {
                    break;
                }
                if (crop.h > 6) {
                    crop.h -= CROP_STEP;
                    crop.y += CROP_STEP/2;
                }
                break;
            case SDL_EVENT_KEY_SHIFT_UP_ARROW:
                if (!crop_enabled) {
                    break;
                }
                if (crop.y + crop.h < 100 && crop.y > 0) {
                    crop.h += CROP_STEP;
                    crop.y -= CROP_STEP/2;
                }
                break;
            case SDL_EVENT_KEY_SHIFT_LEFT_ARROW:
                if (!crop_enabled) {
                    break;
                }
                if (crop.w > 6) {
                    crop.w -= CROP_STEP;
                    crop.x += CROP_STEP/2;
                }
                break;
            case SDL_EVENT_KEY_SHIFT_RIGHT_ARROW:
                if (!crop_enabled) {
                    break;
                }
                if (crop.x + crop.w < 100 && crop.x > 0) {
                    crop.w += CROP_STEP;
                    crop.x -= CROP_STEP/2;
                }
                break;
            case '-':
                if (!crop_enabled) {
                    break;
                }
                if (crop.w > 6 && crop.h > 6) {
                    crop.w -= CROP_STEP;
                    crop.h -= CROP_STEP;
                    crop.x += CROP_STEP/2;
                    crop.y += CROP_STEP/2;
                }
                break;
            case '+': case '=':
                if (!crop_enabled) {
                    break;
                }
                if ((crop.y + crop.h < 100 && crop.y > 0) &&
                    (crop.x + crop.w < 100 && crop.x > 0)) 
                {
                    crop.w += CROP_STEP;
                    crop.h += CROP_STEP;
                    crop.x -= CROP_STEP/2;
                    crop.y -= CROP_STEP/2;
                }
                break;
            case SDL_EVENT_KEY_ESC:
                if (!crop_enabled) {
                    break;
                }
                sdl_play_event_sound();
                crop_enabled = false;
                break;
            case SDL_EVENT_KEY_ENTER:
                if (!crop_enabled) {
                    break;
                }
                sdl_play_event_sound();
                image[crop_idx].crop.x = image[crop_idx].crop.x + crop.x * image[crop_idx].crop.w / 100;
                image[crop_idx].crop.w = crop.w * image[crop_idx].crop.w / 100;
                image[crop_idx].crop.y = image[crop_idx].crop.y + crop.y * image[crop_idx].crop.h / 100;
                image[crop_idx].crop.h = crop.h * image[crop_idx].crop.h / 100;
                sdl_destroy_texture(cached_texture[crop_idx]);
                cached_texture[crop_idx] = NULL;
                crop_enabled = false;
                break;
            case 'r':
                if (!crop_enabled) {
                    break;
                }
                if (memcmp(&image[crop_idx].crop, &crop_uncropped, sizeof(crop_t)) != 0) {
                    sdl_play_event_sound();
                    image[crop_idx].crop = crop_uncropped;
                    sdl_destroy_texture(cached_texture[crop_idx]);
                    cached_texture[crop_idx] = NULL;
                }
                break;
            case 'R': {
                bool did_some_work = false;
                for (i = 0; i < max_image; i++) {
                    if (memcmp(&image[i].crop, &crop_uncropped, sizeof(crop_t)) != 0) {
                        image[i].crop = crop_uncropped;
                        sdl_destroy_texture(cached_texture[i]);
                        cached_texture[i] = NULL;
                        did_some_work = true;
                    }
                }
                if (did_some_work) {
                    sdl_play_event_sound();
                }
                break; }
            
            // a screen shot was taken
            case SDL_EVENT_SCREENSHOT_TAKEN:  
                sdl_play_event_sound();
                break;

            // window event
            case SDL_EVENT_WIN_SIZE_CHANGE:
            case SDL_EVENT_WIN_RESTORED:
                for (i = 0; i < MAX_IMAGE; i++) {
                    sdl_destroy_texture(cached_texture[i]);
                    cached_texture[i] = NULL;
                }
                break;

            // ignore any other events
            default:                          
                unsupported_event = true;
                break;
            }

            // if we've processed an event then break to cause screen redraw
            if (!unsupported_event) {
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
    single jpeg or png output file. Each of the images can optionally be cropped.\n\
\n\
OPTIONS\n\
    -i WxH      : initial width/height of each image, default 320x240 \n\
    -i W        : initial width of each image, the image height will be\n\
                  set using 1.333 aspect ratio\n\
    -o WxH      : initial width/height of the combined output\n\
    -o W        : initial width of the combined ouput, the combined output \n\
                  height will be set based on the number of rows and cols\n\
                  and 1.333 aspect ratio \n\
    -c NUM      : initial number of columns, default is\n\
                  based on number of images and layout\n\
    -f NAME     : output filename, must have .jpg or .png extension,\n\
                  default 'out.jpg'\n\
    -l LAYOUT   : 1 = equal size; 2 = first image double size, default 1\n\
    -b COLOR    : select border color, default GREEN, choices are \n\
                  NONE, PURPLE, BLUE, LIGHT_BLUE, GREEN, YELLOW, ORANGE, \n\
                  PINK, RED, GRAY, WHITE, BLACK \n\
    -k n,x,y,w,h: crop image n; x,y,w,h are in percent; x,y are the upper left of\n\
                  the crop area; w,h are the size of the crop area\n\
    -z          : enable batch mode, the combined output will be written and\n\
                  this program terminates\n\
    -h          : help\n\
\n\
    -i and -o can not be combined\n\
\n\
RUN TIME CONTROLS - WHEN NOT IN BATCH MODE\n\
    General Keyboard Controls\n\
        w      write file containing the combined images\n\
        q      exit the program\n\
        c, C   decrease or increase the number of image columns\n\
\n\
    Window Resize Control\n\
        mouse\n\
\n\
    Crop Image Keyboard Controls\n\
        Tab, ShiftTab     select an image to be cropped\n\
        arrow keys        adjust the position of the crop area\n\
        shift arrow keys  adjust the aspect ratio of the crop area\n\
        -, +, =           adjust the size of the crop area (= is same as +)\n\
        Enter             apply the crop\n\
        Esc               exit crop mode without applying the crop\n\
        r                 reset the selected image to it's original size\n\
        R                 reset all images to their original size\n\
");
}

// -----------------  DRAW IMAGES  --------------------------------------------------------------

void draw_images(void)
{
    static int32_t i;

    sdl_display_init();
    for (i = 0; i < max_pane; i++) {
        rect_t * texture_dest_pane = (border_color == NO_BORDER ? &pane_full[i] : &pane[i]);

        // if image exists then render it, based on its crop value;
        // if we have a cached texture then use the cached texture (it is more efficient)
        if (image[i].width != 0) {
            if (cached_texture[i] == NULL) {
                texture_t texture;
                texture = sdl_create_texture(
                                nearbyint(image[i].width * image[i].crop.w / 100),
                                nearbyint(image[i].height * image[i].crop.h / 100));
                sdl_update_texture(
                                texture,
                                image[i].pixels + BYTES_PER_PIXEL *
                                    (  (int)nearbyint(image[i].width * image[i].crop.x / 100) +
                                       (int)nearbyint(image[i].height * image[i].crop.y / 100) * image[i].width  ),
                                image[i].width);
                sdl_render_texture(texture, texture_dest_pane);
                sdl_destroy_texture(texture);
                cached_texture[i] = sdl_create_texture_from_pane_pixels(texture_dest_pane);
            } else {
                sdl_render_texture(cached_texture[i], texture_dest_pane);
            }
        }

        // if a border is needed then display the border
        if (i < max_image && border_color != NO_BORDER) {
            sdl_render_pane_border(&pane_full[i], border_color);
        }

        // if crop is enabled for the image currently being processed then 
        // draw the crop rectangle
        if (crop_enabled && i == crop_idx) {
            rect_t r;
            r.x = texture_dest_pane->w * crop.x / 100;
            r.y = texture_dest_pane->h * crop.y / 100;
            r.w = texture_dest_pane->w * crop.w / 100;
            r.h = texture_dest_pane->h * crop.h / 100;
            sdl_render_rect(texture_dest_pane, &r, 1, BLACK);
        }
    }
    sdl_display_present();
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
    //    determine a recomended value for cols
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
    // if both win_width and win_height are not supplied
    //    if both image_width and image_height are not supplied
    //        use default image dims
    //    else if just image_height is not supplied
    //        determine image_height based on image_width and DEFAULT_ASPECT_RATIO
    //    endif
    //    determine win dimensions from image dimensions
    // else if win_width is supplied and win_height is not supplied
    //    determine win_height based on win_width, rows, cols and constant scale factor
    // endif
    if (*win_width == 0 && *win_height == 0) {
        if (image_width == 0) {
            image_width  = DEFAULT_IMAGE_WIDTH;
            image_height = DEFAULT_IMAGE_HEIGHT;
        } else if (image_height == 0) {
            image_height = image_width / DEFAULT_ASPECT_RATIO;
        }
        *win_width = image_width * (*cols);
        *win_height = image_height * rows;
    } else if (*win_width != 0 && *win_height == 0) {
        *win_height = (double)(*win_width) / DEFAULT_ASPECT_RATIO * rows / (*cols);
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
    image_width = win_width / cols;
    image_height = win_height / rows;

    // determine pane, pane_full, and max_pane
    if (layout == LAYOUT_EQUAL_SIZE) {
        *max_pane = 0;
        for (r = 0; r < rows; r++) {
            for (c = 0; c < cols; c++) {
                sdl_init_pane(&pane_full[*max_pane], &pane[*max_pane],
                              image_width * c,
                              image_height * r,
                              image_width,
                              image_height);
                (*max_pane)++;
            }
        }
    } else { // LAYOUT_FIRST_IMAGE_DOUBLE_SIZE
        *max_pane = 0;

        // first pane is double size
        sdl_init_pane(&pane_full[*max_pane], &pane[*max_pane],
                      0, 0,
                      2 * image_width,
                      2 * image_height);
        (*max_pane)++;

        // init the rest of the panes
        for (r = 0; r < rows; r++) {
            for (c = 0; c < cols; c++) {
                if (r <= 1 && c <= 1) {
                    continue;
                }
                sdl_init_pane(&pane_full[*max_pane], &pane[*max_pane],
                              image_width * c,
                              image_height * r,
                              image_width,
                              image_height);
                (*max_pane)++;
            }
        }
    }

    // determine win_width_used and win_height_used, these
    // may be slightly less than win_width/height
    *win_width_used = image_width * cols;
    *win_height_used = image_height * rows;
}

