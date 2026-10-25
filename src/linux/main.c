#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <sys/time.h>
#include <time.h>

#define ACEP_5IN65_WIDTH 600
#define ACEP_5IN65_HEIGHT 448
Display* dsp;
Window win;
int screen;
GC gc;

#include "display.h"
#include "fonts/font8x16.h"
#include "fonts/font8x8.h"
#include "gui/image.h"
#include "gui/label.h"
#include "gui/battery_indicator.h"
#include <icons_16.h>

#include "gui.h"
#include "navi/gps.h"

Colormap colormap;
XColor color[TRANSPARENT]; // number of colors
display_t* eink;

font_t f8x8, f8x16;

const uint16_t margin_top = 5;
const uint16_t margin_bottom = 5;
const uint16_t margin_vertical = 10;
const uint16_t margin_left = 5;
const uint16_t margin_right = 5;
const uint16_t margin_horizontal = 10;
const uint8_t zoomlevels[] = { 14, 16 };
uint8_t zoom = 0;

extern error_code_t statusRender(const display_t* dsp, void* comp);
extern void toggleZoom();
extern uint8_t sd_status;
extern void set_path_prefix(char *prefix);


render_t* render_pipeline[RL_MAX]; // maximum number of rendered items
render_t* render_last[RL_MAX];     // pointer to end of render pipeline

typedef enum {
    GPS_FIX_INVALID, /*!< Not fixed */
    GPS_FIX_GPS,     /*!< GPS */
    GPS_FIX_DGPS,    /*!< Differential GPS */
    GPS_FIX_DR = 6,  /*!< Dead Reckoning, valid fix */
} gps_fix_t;

static map_position_t current_position = {
#ifdef NO_GPS
    .longitude = 12.665907,
    .latitude = 47.737665,
    .satellites_in_use = 3,
    .satellites_in_view = 10,
    .fix = GPS_FIX_GPS,
#else
    .fix = GPS_FIX_INVALID,
#endif
};

label_t* clock_label;
label_t* north_indicator_label;
label_t* wifi_indicator_label;
label_t* ble_indicator_label;
label_t* gps_indicator_label;
label_t* sd_indicator_label;

/* the position the simulator moves with the arrow keys */
const map_position_t* gps_get_position(void)
{
    return &current_position;
}

XImage* frame;

error_code_t write_pixel(const display_t* _, int16_t x, int16_t y, uint8_t c)
{
    if (!frame)
        return PM_FAIL;
    x *= 2;
    y *= 2;
    XPutPixel(frame, x, y, color[c].pixel);
    XPutPixel(frame, x+1, y, color[c].pixel);
    XPutPixel(frame, x+1, y+1, color[c].pixel);
    XPutPixel(frame, x, y+1, color[c].pixel);
    return PM_OK;

    XSetForeground(dsp, gc, color[c].pixel);
    XDrawPoint(dsp, win, gc, x, y);
}

void set_short_press_event(void (*event)(void))
{
}

void set_screen_free_function(void (*free_screen_cb)(void))
{
}

/**
 * Add render function to pipeline
 *
 * @return render slot
 */
render_t* add_to_render_pipeline(error_code_t (*render)(const display_t* dsp, void* component),
    void* comp,
    enum RenderLayer layer)
{
    // increase render slot before adding this one. Slot 0 is overflow!
    render_t* rd = RTOS_Malloc(sizeof(render_t));
    if (!rd) {
        return 0;
    }
    rd->render = render;
    rd->comp = comp;

    if (render_pipeline[layer] == NULL) {
        render_pipeline[layer] = rd;
    } else {
        render_last[layer]->next = rd;
    }
    render_last[layer] = rd;

    return rd;
}

void free_render_pipeline(enum RenderLayer layer)
{
    render_t* r = render_pipeline[layer];
    while (r) {
        render_t* rn;
        rn = r;
        r = r->next;
        RTOS_Free(rn);
    }
    render_pipeline[layer] = NULL;
    render_last[layer] = NULL;
}

void free_all_render_pipelines()
{
    for (uint8_t i = 0; i < RL_MAX; i++)
        free_render_pipeline(i);
}

/**
 * Add a prerender callback to pipeline
 *
 * This is called before all other renderers are called.
 *
 * @return render slot
 */
render_t* add_pre_render_callback(error_code_t (*cb)(const display_t* dsp, void* component))
{
    return add_to_render_pipeline(cb, NULL, RL_PRE_RENDER);
}

/**
 * Callbacks from renderer for clock label
 */
error_code_t updateTimeText(const display_t* dsp, void* comp)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    struct tm* timeinfo = localtime(&tv.tv_sec);
    sprintf(clock_label->text, "%02d:%02d", timeinfo->tm_hour,
        timeinfo->tm_min);
    return PM_OK;
}

/*
 * The state of the device, changed with the keys. It is what the power, wifi, ble and sd modules
 * tell the top bar on the device.
 */
static struct {
    uint8_t battery_index;
    bool charging;
    bool wifi_ap;
    uint8_t ble; // 0 off, 1 advertising, 2 connected
} sim = { 0, false, true, 1 };
static const uint8_t sim_battery_levels[] = { 100, 80, 50, 30, 10, 3 };

static battery_indicator_t* battery_indicator;

/* progress boxes of transfers and firmware updates, as gui.c draws them below the top bar */
#define PROGRESS_BOX_TOP (ICON_SIZE + margin_vertical + 10)
#define SIM_FILES 12
static uint8_t sim_upload;   // 0 idle, 1-4 active, 5 complete, 6 stopped
static uint8_t sim_firmware; // 0 idle, 1-5 active (0 to 100%), 6 failed

static void render_progress_boxes(const display_t* dsp)
{
    char text[48];
    uint32_t done = (sim_upload - 1) * 4;
    switch (sim_upload) {
    case 0:
        break;
    case 5:
        snprintf(text, sizeof(text), "Upload complete: %u files", SIM_FILES);
        ui_progress_box(dsp, margin_left, PROGRESS_BOX_TOP, dsp->size.width - margin_horizontal, text, SIM_FILES, SIM_FILES, GREEN);
        break;
    case 6:
        snprintf(text, sizeof(text), "Upload stopped: %u / %u files", 8, SIM_FILES);
        ui_progress_box(dsp, margin_left, PROGRESS_BOX_TOP, dsp->size.width - margin_horizontal, text, 8, SIM_FILES, RED);
        break;
    default:
        snprintf(text, sizeof(text), "Upload %u / %u files", done, SIM_FILES);
        ui_progress_box(dsp, margin_left, PROGRESS_BOX_TOP, dsp->size.width - margin_horizontal, text, done, SIM_FILES, GREEN);
    }
    if (sim_firmware == 6) {
        ui_progress_box(dsp, margin_left, PROGRESS_BOX_TOP, dsp->size.width - margin_horizontal, "Firmware update failed", 0, 100, RED);
    } else if (sim_firmware) {
        uint32_t percent = (sim_firmware - 1) * 25;
        snprintf(text, sizeof(text), "Firmware update %u%%", percent);
        ui_progress_box(dsp, margin_left, PROGRESS_BOX_TOP, dsp->size.width - margin_horizontal, text, percent, 100, GREEN);
    }
}

static int sprint_battery_percent(char* buffer, const char* format, ...)
{
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, BATTERY_CHARGE_STRBUF, format, args);
    va_end(args);
    return 0;
}

static error_code_t battery_indicator_onBeforeRender(const display_t* dsp, void* comp)
{
    battery_indicator->charging = sim.charging;
    battery_indicator_set_level(battery_indicator, sim_battery_levels[sim.battery_index]);
    return PM_OK;
}

static error_code_t wifi_ap_icon_onBeforeRender(const display_t* dsp, void* image)
{
    ((image_t*)image)->data = sim.wifi_ap ? WIFI_AP : NULL;
    return PM_OK;
}

static error_code_t ble_icon_onBeforeRender(const display_t* dsp, void* image)
{
    uint8_t* data = NULL;
    if (sim.ble == 2)
        data = BLE_conn;
    else if (sim.ble == 1)
        data = BLE;
    ((image_t*)image)->data = data;
    return PM_OK;
}

static label_t* create_icon_with_text(const display_t* dsp, uint8_t* icon_data,
    uint16_t left, uint16_t top, char* text, font_t* font)
{
    image_t* img = image_create(icon_data, left, top, ICON_SIZE,
        ICON_SIZE);

    label_t* il = label_create(text, font,
        img->box.left + img->box.width + margin_left, top, 0, 0);
    il->child = img;
    label_shrink_to_text(il);
    il->alignVertical = MIDDLE;
    return il;
}

static void add_icon_with_text_to_pipeline(label_t* il)
{
    add_to_render_pipeline(label_render, il, RL_GUI_ELEMENTS);
    // render image after Label is rendered
    add_to_render_pipeline(image_render, il->child, RL_GUI_ELEMENTS);
}

/* the same components, in the same places and order as the top bar of the device (gui.c) */
static void create_top_bar(const display_t* dsp)
{
    label_t* sb = label_create("", &f8x8, 0, 0, dsp->size.width,
        ICON_SIZE + margin_vertical);

    sb->borderColor = BLACK;
    sb->borderWidth = 1;
    sb->borderLines = ALL_SOLID;
    sb->alignHorizontal = CENTER;
    sb->alignVertical = MIDDLE;
    sb->backgroundColor = WHITE;

    battery_indicator = create_battery_indicator(sb->box.left + margin_left, margin_top,
        sim_battery_levels[sim.battery_index], sim.charging, &f8x8, batlevels, batlevel_images, batlevel_num);
    battery_indicator->save_printf = sprint_battery_percent;
    // the device shows "...%" until the power task read the battery, the other icons are placed behind it
    sprintf(battery_indicator->label_text, "...%%");
    label_shrink_to_text(&battery_indicator->label);
    battery_indicator->label.onBeforeRender = battery_indicator_onBeforeRender;

    north_indicator_label = create_icon_with_text(dsp, norden,
        battery_indicator->label.box.left + battery_indicator->label.box.width + margin_horizontal,
        margin_top, "", &f8x8);

    wifi_indicator_label = create_icon_with_text(dsp, WIFI_AP,
        north_indicator_label->box.left + north_indicator_label->box.width + margin_horizontal,
        margin_top, "", &f8x8);
    ((image_t*)wifi_indicator_label->child)->onBeforeRender = wifi_ap_icon_onBeforeRender;

    ble_indicator_label = create_icon_with_text(dsp, BLE,
        wifi_indicator_label->box.left + wifi_indicator_label->box.width + margin_horizontal,
        margin_top, "", &f8x8);
    ((image_t*)ble_indicator_label->child)->onBeforeRender = ble_icon_onBeforeRender;

    char* GPSView = RTOS_Malloc(sizeof(char) * 5);
    GPSView[0] = 0;
    gps_indicator_label = create_icon_with_text(dsp, noGPS,
        dsp->size.width - ICON_SIZE - (2 * margin_right) - 16, margin_top, GPSView, &f8x8);

    sd_indicator_label = create_icon_with_text(dsp, noSD,
        gps_indicator_label->box.left - 2 * ICON_SIZE - margin_right, margin_top, "",
        &f8x8);

    add_to_render_pipeline(label_render, sb, RL_GUI_BACKGROUND);
    add_to_render_pipeline(label_render, &battery_indicator->label, RL_GUI_ELEMENTS);
    add_to_render_pipeline(image_render, &battery_indicator->image, RL_GUI_ELEMENTS);
    add_icon_with_text_to_pipeline(north_indicator_label);
    add_icon_with_text_to_pipeline(wifi_indicator_label);
    add_icon_with_text_to_pipeline(ble_indicator_label);
    add_icon_with_text_to_pipeline(gps_indicator_label);
    add_icon_with_text_to_pipeline(sd_indicator_label);

#ifdef CLOCK
    /* global clock label. */
    char* time = RTOS_Malloc(6);
    clock_label = label_create(time, &f8x8, sb->box.left, sb->box.top,
        sb->box.width, sb->box.height);
    clock_label->alignVertical = MIDDLE;
    clock_label->alignHorizontal = CENTER;
    clock_label->onBeforeRender = updateTimeText;
    add_to_render_pipeline(label_render, clock_label, RL_GUI_ELEMENTS);
#endif
}

GC create_gc(Display* display, Window win, int reverse_video)
{
    GC gc;                       /* handle of newly created GC.  */
    unsigned long valuemask = 0; /* which values in 'values' to  */
                                 /* check when creating the GC.  */
    XGCValues values;            /* initial values for the GC.   */
    unsigned int line_width = 2; /* line width for the GC.       */
    int line_style = LineSolid;  /* style for lines drawing and  */
    int cap_style = CapButt;     /* style of the line's edje and */
    int join_style = JoinBevel;  /*  joined lines.		*/
    int screen_num = DefaultScreen(display);

    gc = XCreateGC(display, win, valuemask, &values);
    if (gc < 0) {
        fprintf(stderr, "XCreateGC: \n");
    }

    /* allocate foreground and background colors for this GC. */
    if (reverse_video) {
        XSetForeground(display, gc, WhitePixel(display, screen_num));
        XSetBackground(display, gc, BlackPixel(display, screen_num));
    } else {
        XSetForeground(display, gc, BlackPixel(display, screen_num));
        XSetBackground(display, gc, WhitePixel(display, screen_num));
    }

    /* define the style of lines that will be drawn using this GC. */
    XSetLineAttributes(display, gc,
        line_width, line_style, cap_style, join_style);

    /* define the fill style for the GC. to be 'solid filling'. */
    XSetFillStyle(display, gc, FillSolid);

    return gc;
}

/**
 * Return color for x/y pixel
 */
uint8_t Decompress_Pixel(rect_t* size, int16_t x, int16_t y,
    const uint8_t* data)
{
    uint32_t pos = (y * size->width) + x;

    if (pos & 0x1) {
        return (data[pos >> 1] & 0x7);
    }
    return ((data[pos >> 1] >> 4) & 0x7);
}

void render()
{
    render_t* rd;
    //    uint64_t start = esp_timer_get_time();
    display_fill(eink, WHITE);
    for (uint8_t layer = 0; layer < RL_MAX; layer++) {
        rd = render_pipeline[layer];
        while (rd) {
            if (rd->render)
                rd->render(eink, rd->comp);
            rd = rd->next;
        }
    }
    render_progress_boxes(eink);
}

void trigger_rendering()
{
    render();
}

int save_ximage_pnm(XImage* img, const char* pnmname, int type)
{
    int ret, x, y;
    unsigned long pixel;
    // FAIL_ON(!img || !pnmname || type<=0, "bad argument(s)");
    FILE* f = fopen(pnmname, "w");
    fprintf(f, "P%d\n%d %d\n255\n", type, img->width, img->height);
    for (y = 0; y < img->height; y++) {
        for (x = 0; x < img->width; x++) {
            pixel = XGetPixel(img, x, y);
            if (type == 3) {
                fprintf(f, "%ld %ld %ld\n",
                    pixel >> 16, (pixel & 0x00ff00) >> 8, pixel & 0x0000ff);
            } else if (type == 4) {
                fprintf(f, "%c%c%c",
                    (char)(pixel >> 16),
                    (char)((pixel & 0x00ff00) >> 8),
                    (char)(pixel & 0x0000ff));

            } else {
                printf("PnM type %d not supported!", type);
                ret = 0;
                goto close_file;
            }
        }
    }
    ret = 1;

close_file:
    fclose(f);
    return ret;
}

/*
 * usage: program [--screenshot file.pnm] [--map file.map] [--zoom 14|16] [--no-fix] [lat lon [tile directory]]
 *
 * With --map (or INDIANAVI_MAP) the map is drawn from the Mapsforge file instead of tiles.
 * --no-fix starts without a GPS fix, the map screen shows that the position is searched.
 * Arrow keys move the position, enter toggles the zoom level, f toggles the fix, space saves frame.pnm.
 */
static const char* screenshot_file;

int main(int argc, char* argv[])
{
    XEvent evt;
    Status rc;
    dsp = XOpenDisplay(0);

    if (dsp == NULL) {
        fprintf(stderr, "Cannot open display\n");
        exit(1);
    }

    screen = DefaultScreen(dsp);

    win = XCreateSimpleWindow(dsp, RootWindow(dsp, screen),
        100, 100, ACEP_5IN65_HEIGHT*2, ACEP_5IN65_WIDTH*2,
        0, BlackPixel(dsp, screen), WhitePixel(dsp, screen));

    XSelectInput(dsp, win, ExposureMask | KeyPressMask);
    XStoreName(dsp, win, "India Navi - Linux");

    XSizeHints sizehints;
    sizehints.flags = PMinSize | PMaxSize;
    sizehints.min_width = ACEP_5IN65_HEIGHT*2;
    sizehints.max_width = ACEP_5IN65_HEIGHT*2;
    sizehints.min_height = ACEP_5IN65_WIDTH*2;
    sizehints.max_height = ACEP_5IN65_WIDTH*2;
    XSetWMNormalHints(dsp, win, &sizehints);

    XMapWindow(dsp, win);
    gc = create_gc(dsp, win, 0);

    colormap = DefaultColormap(dsp, DefaultScreen(dsp));
    rc = XAllocNamedColor(dsp, colormap, "white", &color[WHITE], &color[WHITE]);
    if (rc == 0) {
        fprintf(stderr, "XAllocNamedColor - failed to allocated 'red' color.\n");
        exit(1);
    }
    rc = XAllocNamedColor(dsp, colormap, "black", &color[BLACK], &color[BLACK]);
    if (rc == 0) {
        fprintf(stderr, "XAllocNamedColor - failed to allocated 'red' color.\n");
        exit(1);
    }
    rc = XAllocNamedColor(dsp, colormap, "red", &color[RED], &color[RED]);
    if (rc == 0) {
        fprintf(stderr, "XAllocNamedColor - failed to allocated 'red' color.\n");
        exit(1);
    }
    rc = XAllocNamedColor(dsp, colormap, "orange", &color[ORANGE], &color[ORANGE]);
    if (rc == 0) {
        fprintf(stderr, "XAllocNamedColor - failed to allocated 'orange' color.\n");
        exit(1);
    }
    rc = XAllocNamedColor(dsp, colormap, "blue", &color[BLUE], &color[BLUE]);
    if (rc == 0) {
        fprintf(stderr, "XAllocNamedColor - failed to allocated 'blue' color.\n");
        exit(1);
    }
    rc = XAllocNamedColor(dsp, colormap, "yellow", &color[YELLOW], &color[YELLOW]);
    if (rc == 0) {
        fprintf(stderr, "XAllocNamedColor - failed to allocated 'yellow' color.\n");
        exit(1);
    }
    rc = XAllocNamedColor(dsp, colormap, "green", &color[GREEN], &color[GREEN]);
    if (rc == 0) {
        fprintf(stderr, "XAllocNamedColor - failed to allocated 'green' color.\n");
        exit(1);
    }
    eink = display_init(ACEP_5IN65_HEIGHT, ACEP_5IN65_WIDTH, 8, DISPLAY_ROTATE_0);
    eink->write_pixel = write_pixel;
    eink->decompress = Decompress_Pixel;

    /* create an image where the eink screne is rendered into*/
    char* data = (char*)malloc(ACEP_5IN65_HEIGHT*2 * ACEP_5IN65_WIDTH*2 * 4);
    frame = XCreateImage(dsp, DefaultVisual(dsp, screen), DefaultDepth(dsp, screen), ZPixmap, 0, data, ACEP_5IN65_HEIGHT*2, ACEP_5IN65_WIDTH*2, 32, 0);

    font_load_from_array(&f8x8, font8x8, font8x8_name);
    font_load_from_array(&f8x16, font8x16, font8x16_name);

    int positional = 0;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--screenshot") == 0 && i + 1 < argc) {
            screenshot_file = argv[++i];
        } else if (strcmp(argv[i], "--map") == 0 && i + 1 < argc) {
            setenv("INDIANAVI_MAP", argv[++i], 1);
        } else if (strcmp(argv[i], "--no-fix") == 0) {
            current_position.fix = GPS_FIX_INVALID;
        } else if (strcmp(argv[i], "--zoom") == 0 && i + 1 < argc) {
            if (atoi(argv[++i]) == 14)
                toggleZoom(); // the screen starts at zoom level 16
        } else if (positional == 0) {
            current_position.latitude = atof(argv[i]);
            positional++;
        } else if (positional == 1) {
            current_position.longitude = atof(argv[i]);
            positional++;
        } else if (positional == 2) {
            set_path_prefix(argv[i]);
            positional++;
        }
    }

    create_top_bar(eink);
    map_screen_create(eink);

    sd_indicator_label->onBeforeRender = statusRender;

    if (screenshot_file) {
        // no need to wait for the window to be shown, the frame is rendered into memory
        render();
        save_ximage_pnm(frame, screenshot_file, 3);
        return 0;
    }

    while (1) {
        XNextEvent(dsp, &evt);
        if (evt.xany.window == win) {
            if (evt.type == Expose) {
                render();
                XPutImage(dsp, win, gc, frame, 0, 0, 0, 0, ACEP_5IN65_HEIGHT*2, ACEP_5IN65_WIDTH*2);
                if (screenshot_file) {
                    save_ximage_pnm(frame, screenshot_file, 3);
                    exit(0);
                }
            }
            if (evt.type == KeyPress) {
                // up - 111
                // down - 116
                // left - 113
                // right - 114
                switch (evt.xkey.keycode) {
                case 113:
                    current_position.longitude -= 0.0002;
                    break;
                case 116:
                    current_position.latitude -= 0.0002;
                    break;
                case 111:
                    current_position.latitude += 0.0002;
                    break;
                case 114:
                    current_position.longitude += 0.0002;
                    break;
                case 41: // f
                    current_position.fix = current_position.fix == GPS_FIX_INVALID ? GPS_FIX_GPS : GPS_FIX_INVALID;
                    printf("GPS fix %s\n", current_position.fix == GPS_FIX_INVALID ? "lost" : "found");
                    break;
                case 65: // spacebar
                    save_ximage_pnm(frame, "frame.pnm", 3);
                    break;
                case 36: // enter
                    toggleZoom();
                    printf("Zoom toggled\n");
                    break;
                default: {
                    // simulated state of the device
                    switch (XLookupKeysym(&evt.xkey, 0)) {
                    case XK_b: // battery level
                        sim.battery_index = (sim.battery_index + 1) % (sizeof(sim_battery_levels));
                        break;
                    case XK_c: // charger
                        sim.charging = !sim.charging;
                        break;
                    case XK_w: // access point
                        sim.wifi_ap = !sim.wifi_ap;
                        break;
                    case XK_t: // bluetooth: off, advertising, connected
                        sim.ble = (sim.ble + 1) % 3;
                        break;
                    case XK_s: // SD card
                        sd_status = (sd_status == PM_OK) ? PM_FAIL : PM_OK;
                        break;
                    case XK_u: // file transfer: idle, running in steps, complete, stopped
                        sim_upload = (sim_upload + 1) % 7;
                        break;
                    case XK_f: // firmware update: idle, running in steps, failed
                        sim_firmware = (sim_firmware + 1) % 7;
                        break;
                    case XK_g: // GNSS fix
                        current_position.fix = current_position.fix == GPS_FIX_INVALID ? GPS_FIX_GPS : GPS_FIX_INVALID;
                        break;
                    default:
                        printf("Unknown button: %d\n", evt.xkey.keycode);
                    }
                }
                }
                render();
                XPutImage(dsp, win, gc, frame, 0, 0, 0, 0, ACEP_5IN65_HEIGHT*2, ACEP_5IN65_WIDTH*2);
            }
        }
    }

    return 0;
}