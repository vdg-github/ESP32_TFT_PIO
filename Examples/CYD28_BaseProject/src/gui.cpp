/**
 * @file gui.cpp
 * @brief Retro audio-player UI for the CYD 2.8" board.
 *
 *   Screens:
 *     - screenPlayer    : disc + vertical VU, track info, volume, transport
 *     - screenSources   : SD files / Radio / TTS / Settings picker
 *     - screenBrowser   : list of audio files on the SD card
 *     - screenRadio     : hard-coded internet radio presets
 *     - screenTTS       : TTS phrase shortcuts
 *     - screenSettings  : system info, LDR readout, WiFi reset
 *
 *   Aesthetic: black background, neon/arcade accent colours,
 *   Montserrat 14/28 for FA symbols, UbuntuCond for text.
 */

#include "gui.h"
#include <WiFi.h>
#include <SD.h>
#include <WiFiManager.h>
#include "CYD28_LDR.h"
#include "CYD28_audio.h"
#include "CYD28_SD.h"

extern WiFiManager wifiManager;
extern LDR ldr;

extern const lv_font_t UbuntuCond11;
extern const lv_font_t UbuntuCond14;
extern const lv_font_t UbuntuCond36;

// ---------------- 8-bit / arcade palette -----------------------------------
#define COL_BG          lv_color_hex(0x0a0820)
#define COL_BG_BAR      lv_color_hex(0x05030f)
#define COL_PANEL       lv_color_hex(0x100a30)
#define COL_PANEL_BRD   lv_color_hex(0x4a30a0)
#define COL_TXT         lv_color_hex(0xfff8e8)
#define COL_TXT_DIM     lv_color_hex(0x9080d0)
#define COL_RED         lv_color_hex(0xff2244)
#define COL_ORANGE      lv_color_hex(0xff7800)
#define COL_YELLOW      lv_color_hex(0xffd000)
#define COL_LIME        lv_color_hex(0x70ff20)
#define COL_GREEN       lv_color_hex(0x00d040)
#define COL_CYAN        lv_color_hex(0x00e8ff)
#define COL_BLUE        lv_color_hex(0x2080ff)
#define COL_MAGENTA     lv_color_hex(0xff30d0)
#define COL_PINK        lv_color_hex(0xff5a8e)
#define COL_PURPLE      lv_color_hex(0x9040ff)
#define COL_AMBER       COL_YELLOW
#define COL_AMBER_DIM   lv_color_hex(0x4a30a0)

// ---------------- screens --------------------------------------------------
static lv_obj_t *screenPlayer;
static lv_obj_t *screenSources;
static lv_obj_t *screenBrowser;
static lv_obj_t *screenRadio;
static lv_obj_t *screenTTS;
static lv_obj_t *screenSettings;

// ---------------- player widgets -------------------------------------------
static lv_obj_t *lblWifi;
static lv_obj_t *lblHeap;
static lv_obj_t *lblHeader;

static lv_obj_t *lblNowPlaying;
static lv_obj_t *lblTrack;
static lv_obj_t *lblFolder;
static lv_obj_t *vuL;
static lv_obj_t *vuR;
static lv_obj_t *volSlider;
static lv_obj_t *lblVol;
static lv_obj_t *btnPlayPause;
static lv_obj_t *lblPlayPause;

// ---------------- spinning disc --------------------------------------------
LV_IMG_DECLARE(disc_img);
static lv_obj_t *imgDisc;
static int32_t   disc_angle = 0;

// ---------------- timers ---------------------------------------------------
static lv_timer_t *tmrVu;
static lv_timer_t *tmrStatus;
static lv_timer_t *tmrSettings;
static lv_timer_t *tmrDisc;

// ---------------- shared styles --------------------------------------------
static lv_style_t st_scr;
static lv_style_t st_panel;
static lv_style_t st_vu_bg;
static lv_style_t st_vu_indic;
static lv_style_t st_btn;
static lv_style_t st_btn_pressed;
static lv_style_t st_chip;

// ---------------- state ----------------------------------------------------
static char  s_track[128]    = "";      // full path of current track
static bool  s_playing      = false;
static char  s_statusBuf[256];

// ---- playlist: audio files in the current folder ----
#define PLAYLIST_MAX 32
#define PATH_LEN     128
static char  s_pl_dir[PATH_LEN]  = "";  // folder of current track
static char  s_pl_files[PLAYLIST_MAX][PATH_LEN]; // filenames only
static int   s_pl_count     = 0;
static int   s_pl_idx       = -1;     // -1 = no track selected

// ---------------- forward decls --------------------------------------------
static void build_player(void);
static void build_sources(void);
static void build_browser(void);
static void build_radio(void);
static void build_tts(void);
static void build_settings(void);

static void playlist_load(const char *dirpath);
static void playlist_play_idx(int idx);
static void set_track(const char *name, bool playing);
static void update_play_pause_btn(void);
static void refresh_volume_label(int pct);
static void refresh_status_bar(void);

static void cb_vu(lv_timer_t *t);
static void cb_status(lv_timer_t *t);
static void cb_settings(lv_timer_t *t);
static void cb_disc(lv_timer_t *t);

static void on_vol_change(lv_event_t *e);
static void on_transport(lv_event_t *e);
static void on_source_pick(lv_event_t *e);
static void on_browser_pick(lv_event_t *e);
static void on_radio_pick(lv_event_t *e);
static void on_tts_pick(lv_event_t *e);
static void on_back_to_player(lv_event_t *e);
static void on_back_to_sources(lv_event_t *e);
static void on_settings_action(lv_event_t *e);
static void on_browser_refresh(lv_event_t *e);

static void browser_populate(lv_obj_t *list);
static void sysinfo_text(char *buf, size_t n);

// ===========================================================================
//                              S T Y L E S
// ===========================================================================
static void init_styles(void)
{
    lv_style_init(&st_scr);
    lv_style_set_bg_color(&st_scr, COL_BG);
    lv_style_set_bg_opa(&st_scr, LV_OPA_COVER);
    lv_style_set_text_color(&st_scr, COL_TXT);
    lv_style_set_text_font(&st_scr, &UbuntuCond14);
    lv_style_set_pad_all(&st_scr, 0);
    lv_style_set_border_width(&st_scr, 0);

    lv_style_init(&st_panel);
    lv_style_set_bg_color(&st_panel, COL_PANEL);
    lv_style_set_bg_opa(&st_panel, LV_OPA_COVER);
    lv_style_set_border_color(&st_panel, COL_PANEL_BRD);
    lv_style_set_border_width(&st_panel, 1);
    lv_style_set_radius(&st_panel, 2);
    lv_style_set_pad_all(&st_panel, 4);
    lv_style_set_text_color(&st_panel, COL_TXT);

    lv_style_init(&st_vu_bg);
    lv_style_set_bg_color(&st_vu_bg, lv_color_hex(0x0a0a0a));
    lv_style_set_bg_opa(&st_vu_bg, LV_OPA_COVER);
    lv_style_set_border_color(&st_vu_bg, COL_AMBER_DIM);
    lv_style_set_border_width(&st_vu_bg, 1);
    lv_style_set_radius(&st_vu_bg, 0);

    lv_style_init(&st_vu_indic);
    lv_style_set_bg_opa(&st_vu_indic, LV_OPA_COVER);
    lv_style_set_bg_color(&st_vu_indic, COL_GREEN);
    lv_style_set_bg_grad_color(&st_vu_indic, COL_RED);
    lv_style_set_bg_grad_dir(&st_vu_indic, LV_GRAD_DIR_VER);
    lv_style_set_radius(&st_vu_indic, 0);

    lv_style_init(&st_btn);
    lv_style_set_bg_color(&st_btn, lv_color_hex(0x141414));
    lv_style_set_bg_opa(&st_btn, LV_OPA_COVER);
    lv_style_set_border_color(&st_btn, COL_AMBER);
    lv_style_set_border_width(&st_btn, 1);
    lv_style_set_radius(&st_btn, 2);
    lv_style_set_text_color(&st_btn, COL_AMBER);
    lv_style_set_text_font(&st_btn, &lv_font_montserrat_14);
    lv_style_set_pad_all(&st_btn, 4);

    lv_style_init(&st_btn_pressed);
    lv_style_set_bg_color(&st_btn_pressed, COL_AMBER);
    lv_style_set_text_color(&st_btn_pressed, lv_color_hex(0x000000));

    lv_style_init(&st_chip);
    lv_style_set_bg_color(&st_chip, lv_color_hex(0x101010));
    lv_style_set_bg_opa(&st_chip, LV_OPA_COVER);
    lv_style_set_border_color(&st_chip, COL_AMBER_DIM);
    lv_style_set_border_width(&st_chip, 1);
    lv_style_set_radius(&st_chip, 12);
    lv_style_set_text_color(&st_chip, COL_TXT);
    lv_style_set_text_font(&st_chip, &UbuntuCond14);
    lv_style_set_pad_hor(&st_chip, 10);
    lv_style_set_pad_ver(&st_chip, 6);
}

// ===========================================================================
//                              I N I T
// ===========================================================================
void gui_init(void)
{
    init_styles();

    screenPlayer   = lv_obj_create(NULL);
    screenSources  = lv_obj_create(NULL);
    screenBrowser  = lv_obj_create(NULL);
    screenRadio    = lv_obj_create(NULL);
    screenTTS      = lv_obj_create(NULL);
    screenSettings = lv_obj_create(NULL);

    lv_obj_add_style(screenPlayer,   &st_scr, 0);
    lv_obj_add_style(screenSources,  &st_scr, 0);
    lv_obj_add_style(screenBrowser,  &st_scr, 0);
    lv_obj_add_style(screenRadio,    &st_scr, 0);
    lv_obj_add_style(screenTTS,      &st_scr, 0);
    lv_obj_add_style(screenSettings, &st_scr, 0);

    build_player();
    build_sources();
    build_browser();
    build_radio();
    build_tts();
    build_settings();

    tmrVu       = lv_timer_create(cb_vu,       60,   NULL);
    tmrStatus   = lv_timer_create(cb_status,   1500, NULL);
    tmrSettings = lv_timer_create(cb_settings, 2000, NULL);
    tmrDisc     = lv_timer_create(cb_disc,     50,   NULL);

    refresh_status_bar();
    lv_scr_load(screenPlayer);
}

// ===========================================================================
//                       S T A T U S   B A R   (shared)
// ===========================================================================
static void add_status_bar(lv_obj_t *scr, lv_obj_t **lblLeft, lv_obj_t **lblRight, lv_obj_t **lblMid)
{
    lv_obj_t *bar = lv_obj_create(scr);
    lv_obj_remove_style_all(bar);
    lv_obj_set_size(bar, 320, 24);
    lv_obj_set_pos(bar, 0, 0);
    lv_obj_set_style_bg_color(bar, lv_color_hex(0x100c00), 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(bar, COL_MAGENTA, 0);
    lv_obj_set_style_border_side(bar, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_width(bar, 1, 0);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);

    *lblLeft = lv_label_create(bar);
    lv_obj_set_style_text_color(*lblLeft, COL_CYAN, 0);
    lv_obj_set_style_text_font(*lblLeft, &lv_font_montserrat_14, 0);
    lv_obj_align(*lblLeft, LV_ALIGN_LEFT_MID, 6, 0);
    lv_label_set_text(*lblLeft, LV_SYMBOL_WIFI " ---");

    *lblMid = lv_label_create(bar);
    lv_obj_set_style_text_color(*lblMid, COL_PINK, 0);
    lv_obj_set_style_text_font(*lblMid, &UbuntuCond14, 0);
    lv_obj_align(*lblMid, LV_ALIGN_CENTER, 0, 0);
    lv_label_set_text(*lblMid, "CYD28 PLAYER");

    *lblRight = lv_label_create(bar);
    lv_obj_set_style_text_color(*lblRight, COL_LIME, 0);
    lv_obj_set_style_text_font(*lblRight, &UbuntuCond14, 0);
    lv_obj_align(*lblRight, LV_ALIGN_RIGHT_MID, -6, 0);
    lv_label_set_text(*lblRight, "--");
}

// ---- per-screen status bar handles ----
static lv_obj_t *sbP_l, *sbP_r, *sbP_m;
static lv_obj_t *sbS_l, *sbS_r, *sbS_m;
static lv_obj_t *sbB_l, *sbB_r, *sbB_m;
static lv_obj_t *sbR_l, *sbR_r, *sbR_m;
static lv_obj_t *sbT_l, *sbT_r, *sbT_m;
static lv_obj_t *sbX_l, *sbX_r, *sbX_m;

static void refresh_status_bar(void)
{
    char left[40];
    char right[40];

    if (WiFi.status() == WL_CONNECTED) {
        IPAddress ip = WiFi.localIP();
        snprintf(left, sizeof(left), LV_SYMBOL_WIFI " %d.%d.%d.%d",
                 ip[0], ip[1], ip[2], ip[3]);
    } else {
        snprintf(left, sizeof(left), LV_SYMBOL_WIFI " offline");
    }
    snprintf(right, sizeof(right), "%uk free", (unsigned)(ESP.getFreeHeap() / 1024));

    lv_obj_t *active = lv_scr_act();
    if (active == screenPlayer)        { lv_label_set_text(sbP_l, left); lv_label_set_text(sbP_r, right); }
    else if (active == screenSources)  { lv_label_set_text(sbS_l, left); lv_label_set_text(sbS_r, right); }
    else if (active == screenBrowser)  { lv_label_set_text(sbB_l, left); lv_label_set_text(sbB_r, right); }
    else if (active == screenRadio)    { lv_label_set_text(sbR_l, left); lv_label_set_text(sbR_r, right); }
    else if (active == screenTTS)      { lv_label_set_text(sbT_l, left); lv_label_set_text(sbT_r, right); }
    else if (active == screenSettings) { lv_label_set_text(sbX_l, left); lv_label_set_text(sbX_r, right); }
}

// ===========================================================================
//                    P L A Y L I S T   H E L P E R S
// ===========================================================================
static bool has_audio_ext(const char *name)
{
    const char *dot = strrchr(name, '.');
    if (!dot) return false;
    return  !strcasecmp(dot, ".mp3") || !strcasecmp(dot, ".wav") ||
            !strcasecmp(dot, ".flac")|| !strcasecmp(dot, ".m4a") ||
            !strcasecmp(dot, ".ogg") || !strcasecmp(dot, ".aac");
}

static int audio_cmp(const void *a, const void *b)
{
    return strcasecmp((const char *)a, (const char *)b);
}

static void playlist_load(const char *dirpath)
{
    s_pl_count = 0;
    s_pl_idx   = -1;
    snprintf(s_pl_dir, sizeof(s_pl_dir), "%s", dirpath);

    File dir = SD.open(dirpath);
    if (!dir || !dir.isDirectory()) {
        if (dir) dir.close();
        return;
    }
    // Collect audio files
    File f = dir.openNextFile();
    while (f && s_pl_count < PLAYLIST_MAX) {
        const char *full = f.name();
        const char *base = strrchr(full, '/');
        base = base ? base + 1 : full;
        if (!f.isDirectory() && has_audio_ext(base)) {
            snprintf(s_pl_files[s_pl_count], PATH_LEN, "%s", base);
            s_pl_count++;
        }
        f = dir.openNextFile();
    }
    dir.close();
    // Sort alphabetically
    if (s_pl_count > 1)
        qsort(s_pl_files, s_pl_count, PATH_LEN, audio_cmp);
}

static void playlist_play_idx(int idx)
{
    if (idx < 0 || idx >= s_pl_count) return;
    s_pl_idx = idx;
    static char path[PATH_LEN + PATH_LEN];
    if (strcmp(s_pl_dir, "/") == 0 || s_pl_dir[0] == 0)
        snprintf(path, sizeof(path), "/%s", s_pl_files[idx]);
    else
        snprintf(path, sizeof(path), "%s/%s", s_pl_dir, s_pl_files[idx]);
    audioConnecttoSD(path);
    set_track(path, true);
}

// ===========================================================================
//                         P L A Y E R   S C R E E N
// ===========================================================================
static lv_obj_t *make_transport_btn(lv_obj_t *parent, const char *sym, int x_pct,
                                    int idx, lv_color_t accent)
{
    lv_obj_t *b = lv_btn_create(parent);
    lv_obj_remove_style_all(b);
    lv_obj_add_style(b, &st_btn, 0);
    lv_obj_add_style(b, &st_btn_pressed, LV_STATE_PRESSED);
    lv_obj_set_size(b, 56, 38);
    lv_obj_align(b, LV_ALIGN_LEFT_MID, x_pct, 0);
    lv_obj_set_style_border_color(b, accent, 0);
    lv_obj_set_style_text_color(b, accent, 0);
    lv_obj_set_style_bg_color(b, accent, LV_STATE_PRESSED);

    lv_obj_t *lbl = lv_label_create(b);
    lv_label_set_text(lbl, sym);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
    lv_obj_center(lbl);

    lv_obj_add_event_cb(b, on_transport, LV_EVENT_CLICKED, (void*)(intptr_t)idx);
    return b;
}

static void build_player(void)
{
    add_status_bar(screenPlayer, &sbP_l, &sbP_r, &sbP_m);

    // ---- Spinning disc (top-left) ----
    imgDisc = lv_img_create(screenPlayer);
    lv_img_set_src(imgDisc, &disc_img);
    lv_obj_set_pos(imgDisc, 4, 26);
    lv_img_set_pivot(imgDisc, 40, 40);
    lv_img_set_antialias(imgDisc, false);
    lv_img_set_angle(imgDisc, 0);

    // ---- VU labels (L / R) above bars, right of disc ----
    lv_obj_t *vuLlbl = lv_label_create(screenPlayer);
    lv_obj_set_style_text_color(vuLlbl, COL_CYAN, 0);
    lv_obj_set_style_text_font(vuLlbl, &lv_font_montserrat_14, 0);
    lv_obj_set_pos(vuLlbl, 92, 26);
    lv_label_set_text(vuLlbl, "L");

    lv_obj_t *vuRlbl = lv_label_create(screenPlayer);
    lv_obj_set_style_text_color(vuRlbl, COL_MAGENTA, 0);
    lv_obj_set_style_text_font(vuRlbl, &lv_font_montserrat_14, 0);
    lv_obj_set_pos(vuRlbl, 108, 26);
    lv_label_set_text(vuRlbl, "R");

    // ---- Vertical VU bars (right of disc) ----
    vuL = lv_bar_create(screenPlayer);
    lv_obj_remove_style_all(vuL);
    lv_obj_add_style(vuL, &st_vu_bg, LV_PART_MAIN);
    lv_obj_add_style(vuL, &st_vu_indic, LV_PART_INDICATOR);
    lv_obj_set_size(vuL, 14, 74);
    lv_obj_set_pos(vuL, 92, 40);
    lv_bar_set_range(vuL, 0, 100);
    lv_bar_set_value(vuL, 0, LV_ANIM_OFF);

    vuR = lv_bar_create(screenPlayer);
    lv_obj_remove_style_all(vuR);
    lv_obj_add_style(vuR, &st_vu_bg, LV_PART_MAIN);
    lv_obj_add_style(vuR, &st_vu_indic, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(vuR, COL_CYAN, LV_PART_INDICATOR);
    lv_obj_set_style_bg_grad_color(vuR, COL_MAGENTA, LV_PART_INDICATOR);
    lv_obj_set_size(vuR, 14, 74);
    lv_obj_set_pos(vuR, 108, 40);
    lv_bar_set_range(vuR, 0, 100);
    lv_bar_set_value(vuR, 0, LV_ANIM_OFF);

    // ---- Track info area (right of disc+VU, below status bar) ----
    // "NOW PLAYING" header
    lblNowPlaying = lv_label_create(screenPlayer);
    lv_obj_set_style_text_color(lblNowPlaying, COL_PINK, 0);
    lv_obj_set_style_text_font(lblNowPlaying, &lv_font_montserrat_14, 0);
    lv_obj_set_pos(lblNowPlaying, 128, 28);
    lv_label_set_text(lblNowPlaying, LV_SYMBOL_PLAY " NOW PLAYING");

    // Track name (large, scrolling)
    lblTrack = lv_label_create(screenPlayer);
    lv_obj_set_style_text_color(lblTrack, COL_YELLOW, 0);
    lv_obj_set_style_text_font(lblTrack, &UbuntuCond36, 0);
    lv_obj_set_width(lblTrack, 184);
    lv_label_set_long_mode(lblTrack, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_pos(lblTrack, 128, 48);
    lv_label_set_text(lblTrack, "[ STANDBY ]");

    // Folder / album name
    lblFolder = lv_label_create(screenPlayer);
    lv_obj_set_style_text_color(lblFolder, COL_TXT_DIM, 0);
    lv_obj_set_style_text_font(lblFolder, &UbuntuCond14, 0);
    lv_obj_set_width(lblFolder, 184);
    lv_label_set_long_mode(lblFolder, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_pos(lblFolder, 128, 84);
    lv_label_set_text(lblFolder, "");

    // ---- Volume slider (below disc/VU area) ----
    lv_obj_t *vTag = lv_label_create(screenPlayer);
    lv_obj_set_style_text_color(vTag, COL_PURPLE, 0);
    lv_obj_set_style_text_font(vTag, &UbuntuCond14, 0);
    lv_obj_set_pos(vTag, 8, 118);
    lv_label_set_text(vTag, "VOL");

    volSlider = lv_slider_create(screenPlayer);
    lv_obj_set_size(volSlider, 230, 8);
    lv_obj_set_pos(volSlider, 36, 120);
    lv_slider_set_range(volSlider, 0, 100);
    lv_obj_set_style_bg_color(volSlider, COL_PANEL, LV_PART_MAIN);
    lv_obj_set_style_border_color(volSlider, COL_PURPLE, LV_PART_MAIN);
    lv_obj_set_style_border_width(volSlider, 1, LV_PART_MAIN);
    lv_obj_set_style_bg_color(volSlider, COL_PINK, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(volSlider, COL_CYAN, LV_PART_KNOB);
    lv_obj_set_style_pad_all(volSlider, 4, LV_PART_KNOB);
    lv_obj_add_event_cb(volSlider, on_vol_change, LV_EVENT_VALUE_CHANGED, NULL);

    lblVol = lv_label_create(screenPlayer);
    lv_obj_set_style_text_color(lblVol, COL_PINK, 0);
    lv_obj_set_style_text_font(lblVol, &UbuntuCond14, 0);
    lv_obj_set_pos(lblVol, 272, 116);
    lv_label_set_text(lblVol, "---%");

    int pct = audioGetVolumePerCent();
    lv_slider_set_value(volSlider, pct, LV_ANIM_OFF);
    refresh_volume_label(pct);

    // ---- Transport bar (y=136..236) ----
    lv_obj_t *tbar = lv_obj_create(screenPlayer);
    lv_obj_remove_style_all(tbar);
    lv_obj_set_size(tbar, 320, 104);
    lv_obj_set_pos(tbar, 0, 136);
    lv_obj_set_style_bg_color(tbar, lv_color_hex(0x0a0700), 0);
    lv_obj_set_style_bg_opa(tbar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(tbar, COL_AMBER_DIM, 0);
    lv_obj_set_style_border_side(tbar, LV_BORDER_SIDE_TOP, 0);
    lv_obj_set_style_border_width(tbar, 1, 0);
    lv_obj_clear_flag(tbar, LV_OBJ_FLAG_SCROLLABLE);

    // idx 0=PREV, 1=PLAY/PAUSE, 2=NEXT, 3=FOLDER
    make_transport_btn(tbar, LV_SYMBOL_PREV,     4,   0, COL_CYAN);
    btnPlayPause = make_transport_btn(tbar, LV_SYMBOL_PLAY,  66,  1, COL_LIME);
    // Cache the label so we can toggle between PLAY / PAUSE
    lblPlayPause = lv_obj_get_child(btnPlayPause, 0);

    make_transport_btn(tbar, LV_SYMBOL_NEXT,      128, 2, COL_YELLOW);
    make_transport_btn(tbar, LV_SYMBOL_LIST,      190, 3, COL_MAGENTA);

    // "Sources" button on the far right
    make_transport_btn(tbar, LV_SYMBOL_AUDIO,     252, 4, COL_CYAN);

    lv_obj_clear_flag(screenPlayer, LV_OBJ_FLAG_SCROLLABLE);
}

// ===========================================================================
//                       S O U R C E S   S C R E E N
// ===========================================================================
static lv_obj_t *make_big_btn(lv_obj_t *parent, const char *sym, const char *text,
                              int x, int y, int w, int h, int idx,
                              lv_event_cb_t cb, lv_color_t accent)
{
    lv_obj_t *b = lv_btn_create(parent);
    lv_obj_remove_style_all(b);
    lv_obj_add_style(b, &st_btn, 0);
    lv_obj_add_style(b, &st_btn_pressed, LV_STATE_PRESSED);
    lv_obj_set_size(b, w, h);
    lv_obj_set_pos(b, x, y);
    lv_obj_set_style_border_color(b, accent, 0);
    lv_obj_set_style_border_width(b, 2, 0);
    lv_obj_set_style_bg_color(b, accent, LV_STATE_PRESSED);

    lv_obj_t *col = lv_obj_create(b);
    lv_obj_remove_style_all(col);
    lv_obj_set_size(col, w - 8, h - 8);
    lv_obj_center(col);
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(col, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(col, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(col, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *symL = lv_label_create(col);
    lv_label_set_text(symL, sym);
    lv_obj_set_style_text_font(symL, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(symL, accent, 0);

    lv_obj_t *txtL = lv_label_create(col);
    lv_label_set_text(txtL, text);
    lv_obj_set_style_text_font(txtL, &UbuntuCond14, 0);
    lv_obj_set_style_text_color(txtL, COL_TXT, 0);

    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, (void*)(intptr_t)idx);
    return b;
}

static void build_sources(void)
{
    add_status_bar(screenSources, &sbS_l, &sbS_r, &sbS_m);

    lv_obj_t *title = lv_label_create(screenSources);
    lv_obj_set_style_text_color(title, COL_PINK, 0);
    lv_obj_set_style_text_font(title, &UbuntuCond14, 0);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 8, 30);
    lv_label_set_text(title, "PICK A SOURCE");

    make_big_btn(screenSources, LV_SYMBOL_SD_CARD,  "SD FILES",  8,   56, 148, 70, 0, on_source_pick, COL_LIME);
    make_big_btn(screenSources, LV_SYMBOL_AUDIO,    "RADIO",     164, 56, 148, 70, 1, on_source_pick, COL_CYAN);
    make_big_btn(screenSources, LV_SYMBOL_KEYBOARD, "SPEECH",    8,   132,148, 70, 2, on_source_pick, COL_PINK);
    make_big_btn(screenSources, LV_SYMBOL_SETTINGS, "SETTINGS",  164, 132,148, 70, 3, on_source_pick, COL_YELLOW);

    lv_obj_t *back = lv_btn_create(screenSources);
    lv_obj_remove_style_all(back);
    lv_obj_add_style(back, &st_btn, 0);
    lv_obj_add_style(back, &st_btn_pressed, LV_STATE_PRESSED);
    lv_obj_set_size(back, 88, 28);
    lv_obj_align(back, LV_ALIGN_BOTTOM_LEFT, 8, -6);
    lv_obj_t *bl = lv_label_create(back);
    lv_label_set_text(bl, LV_SYMBOL_LEFT " PLAYER");
    lv_obj_center(bl);
    lv_obj_add_event_cb(back, on_back_to_player, LV_EVENT_CLICKED, NULL);

    lv_obj_clear_flag(screenSources, LV_OBJ_FLAG_SCROLLABLE);
}

// ===========================================================================
//                       B R O W S E R   S C R E E N   (SD)
// ===========================================================================
static lv_obj_t *browserList;

// ---- file browser navigation state ----
#define BROWSER_MAX_ENTRIES 32
#define BROWSER_PATH_LEN    96

static char browser_cwd[BROWSER_PATH_LEN] = "/music";
static char browser_entries[BROWSER_MAX_ENTRIES][BROWSER_PATH_LEN];
static bool browser_is_dir[BROWSER_MAX_ENTRIES];
static int  browser_n_entries = 0;

static void path_join(char *out, size_t cap, const char *base, const char *name)
{
    if (!base[0] || (base[0] == '/' && base[1] == 0))
        snprintf(out, cap, "/%s", name);
    else
        snprintf(out, cap, "%s/%s", base, name);
}

static void path_parent(char *out, size_t cap, const char *p)
{
    snprintf(out, cap, "%s", p);
    char *slash = strrchr(out, '/');
    if (!slash) { out[0] = '/'; out[1] = 0; return; }
    if (slash == out) { out[1] = 0; return; }
    *slash = 0;
}

static void browser_populate(lv_obj_t *list)
{
    lv_obj_clean(list);
    browser_n_entries = 0;
    static const lv_color_t accents[] = {
        lv_color_hex(0x70ff20), lv_color_hex(0x00e8ff),
        lv_color_hex(0xffd000), lv_color_hex(0xff5a8e),
        lv_color_hex(0x9040ff), lv_color_hex(0xff30d0),
    };

    if (SD.cardType() == CARD_NONE) {
        lv_obj_t *e = lv_list_add_text(list, "SD card not mounted");
        lv_obj_set_style_text_color(e, COL_RED, 0);
        return;
    }

    char head[BROWSER_PATH_LEN + 8];
    snprintf(head, sizeof(head), "[ %s ]", browser_cwd);
    lv_obj_t *h = lv_list_add_text(list, head);
    lv_obj_set_style_text_color(h, COL_PINK, 0);
    lv_obj_set_style_text_font(h, &UbuntuCond14, 0);

    File dir = SD.open(browser_cwd);
    if (!dir || !dir.isDirectory()) {
        if (dir) dir.close();
        if (strcmp(browser_cwd, "/") != 0) {
            snprintf(browser_cwd, sizeof(browser_cwd), "/");
            browser_populate(list);
            return;
        }
        lv_obj_t *e = lv_list_add_text(list, "directory missing");
        lv_obj_set_style_text_color(e, COL_RED, 0);
        return;
    }

    if (strcmp(browser_cwd, "/") != 0 && browser_n_entries < BROWSER_MAX_ENTRIES) {
        snprintf(browser_entries[browser_n_entries], BROWSER_PATH_LEN, "..");
        browser_is_dir[browser_n_entries] = true;
        lv_obj_t *btn = lv_list_add_btn(list, LV_SYMBOL_UP, "..");
        lv_obj_set_style_bg_color(btn, COL_PANEL, 0);
        lv_obj_set_style_text_color(btn, COL_CYAN, 0);
        lv_obj_set_style_text_font(btn, &lv_font_montserrat_14, 0);
        lv_obj_set_style_border_color(btn, COL_CYAN, 0);
        lv_obj_set_style_border_side(btn, LV_BORDER_SIDE_BOTTOM, 0);
        lv_obj_set_style_border_width(btn, 1, 0);
        lv_obj_add_event_cb(btn, on_browser_pick, LV_EVENT_CLICKED,
                            (void*)(intptr_t)browser_n_entries);
        browser_n_entries++;
    }

    for (int pass = 0; pass < 2 && browser_n_entries < BROWSER_MAX_ENTRIES; pass++) {
        dir.rewindDirectory();
        File f = dir.openNextFile();
        while (f && browser_n_entries < BROWSER_MAX_ENTRIES) {
            const char *full = f.name();
            const char *base = strrchr(full, '/');
            base = base ? base + 1 : full;
            bool is_dir = f.isDirectory();
            bool show = (pass == 0) ? is_dir : (!is_dir && has_audio_ext(base));
            if (show) {
                int idx = browser_n_entries;
                snprintf(browser_entries[idx], BROWSER_PATH_LEN, "%s", base);
                browser_is_dir[idx] = is_dir;
                lv_color_t a = accents[idx % 6];
                lv_obj_t *btn = lv_list_add_btn(
                    list,
                    is_dir ? LV_SYMBOL_DIRECTORY : LV_SYMBOL_AUDIO,
                    base);
                lv_obj_set_style_bg_color(btn, idx & 1 ? COL_PANEL : COL_BG, 0);
                lv_obj_set_style_text_color(btn, a, 0);
                lv_obj_set_style_text_font(btn, &lv_font_montserrat_14, 0);
                lv_obj_set_style_border_color(btn, a, 0);
                lv_obj_set_style_border_side(btn, LV_BORDER_SIDE_BOTTOM, 0);
                lv_obj_set_style_border_width(btn, 1, 0);
                lv_obj_add_event_cb(btn, on_browser_pick, LV_EVENT_CLICKED,
                                    (void*)(intptr_t)idx);
                browser_n_entries++;
            }
            f = dir.openNextFile();
        }
    }
    dir.close();

    if (browser_n_entries == 0 ||
        (browser_n_entries == 1 && browser_is_dir[0])) {
        lv_obj_t *e = lv_list_add_text(list, "(empty)");
        lv_obj_set_style_text_color(e, COL_TXT_DIM, 0);
    }
}

static void build_browser(void)
{
    add_status_bar(screenBrowser, &sbB_l, &sbB_r, &sbB_m);

    lv_obj_t *title = lv_label_create(screenBrowser);
    lv_obj_set_style_text_color(title, COL_LIME, 0);
    lv_obj_set_style_text_font(title, &UbuntuCond14, 0);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 8, 30);
    lv_label_set_text(title, "SD FILES");

    browserList = lv_list_create(screenBrowser);
    lv_obj_set_size(browserList, 304, 148);
    lv_obj_align(browserList, LV_ALIGN_TOP_LEFT, 8, 48);
    lv_obj_set_style_bg_color(browserList, COL_BG, 0);
    lv_obj_set_style_border_color(browserList, COL_AMBER_DIM, 0);
    lv_obj_set_style_border_width(browserList, 1, 0);
    lv_obj_set_style_pad_all(browserList, 2, 0);
    lv_obj_set_style_radius(browserList, 0, 0);

    lv_obj_t *back = lv_btn_create(screenBrowser);
    lv_obj_remove_style_all(back);
    lv_obj_add_style(back, &st_btn, 0);
    lv_obj_add_style(back, &st_btn_pressed, LV_STATE_PRESSED);
    lv_obj_set_size(back, 88, 28);
    lv_obj_align(back, LV_ALIGN_BOTTOM_LEFT, 8, -6);
    lv_obj_t *bl = lv_label_create(back);
    lv_label_set_text(bl, LV_SYMBOL_LEFT " BACK");
    lv_obj_center(bl);
    lv_obj_add_event_cb(back, on_back_to_sources, LV_EVENT_CLICKED, NULL);

    lv_obj_t *refresh = lv_btn_create(screenBrowser);
    lv_obj_remove_style_all(refresh);
    lv_obj_add_style(refresh, &st_btn, 0);
    lv_obj_add_style(refresh, &st_btn_pressed, LV_STATE_PRESSED);
    lv_obj_set_size(refresh, 88, 28);
    lv_obj_align(refresh, LV_ALIGN_BOTTOM_RIGHT, -8, -6);
    lv_obj_t *rl = lv_label_create(refresh);
    lv_label_set_text(rl, LV_SYMBOL_REFRESH " RESCAN");
    lv_obj_center(rl);
    lv_obj_add_event_cb(refresh, on_browser_refresh, LV_EVENT_CLICKED, NULL);

    lv_obj_clear_flag(screenBrowser, LV_OBJ_FLAG_SCROLLABLE);
}

// ===========================================================================
//                          R A D I O   S C R E E N
// ===========================================================================
typedef struct {
    const char *label;
    const char *url;
} radio_entry_t;

static const radio_entry_t radio_presets[] = {
    { "Radio Paradise (FLAC)", "http://stream.radioparadise.com/flac" },
    { "SomaFM Groove Salad",   "http://ice1.somafm.com/groovesalad-128-mp3" },
    { "SomaFM Drone Zone",     "http://ice1.somafm.com/dronezone-128-mp3" },
    { "BBC World Service",     "http://stream.live.vc.bbcmedia.co.uk/bbc_world_service" },
    { "Sample (Miss Marple)",  "https://github.com/schreibfaul1/ESP32-audioI2S/raw/master/additional_info/Testfiles/Miss-Marple.m4a" },
};
static const int radio_count = sizeof(radio_presets) / sizeof(radio_presets[0]);

static void build_radio(void)
{
    add_status_bar(screenRadio, &sbR_l, &sbR_r, &sbR_m);

    lv_obj_t *title = lv_label_create(screenRadio);
    lv_obj_set_style_text_color(title, COL_CYAN, 0);
    lv_obj_set_style_text_font(title, &UbuntuCond14, 0);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 8, 30);
    lv_label_set_text(title, "RADIO PRESETS");

    lv_obj_t *list = lv_list_create(screenRadio);
    lv_obj_set_size(list, 304, 148);
    lv_obj_align(list, LV_ALIGN_TOP_LEFT, 8, 48);
    lv_obj_set_style_bg_color(list, COL_BG, 0);
    lv_obj_set_style_border_color(list, COL_AMBER_DIM, 0);
    lv_obj_set_style_border_width(list, 1, 0);
    lv_obj_set_style_pad_all(list, 2, 0);
    lv_obj_set_style_radius(list, 0, 0);

    const lv_color_t r_accents[] = { COL_CYAN, COL_LIME, COL_YELLOW, COL_PINK, COL_MAGENTA };
    for (int i = 0; i < radio_count; i++) {
        lv_obj_t *btn = lv_list_add_btn(list, LV_SYMBOL_AUDIO, radio_presets[i].label);
        lv_color_t a = r_accents[i % 5];
        lv_obj_set_style_bg_color(btn, i & 1 ? COL_PANEL : COL_BG, 0);
        lv_obj_set_style_text_color(btn, a, 0);
        lv_obj_set_style_text_font(btn, &lv_font_montserrat_14, 0);
        lv_obj_set_style_border_color(btn, a, 0);
        lv_obj_set_style_border_side(btn, LV_BORDER_SIDE_BOTTOM, 0);
        lv_obj_set_style_border_width(btn, 1, 0);
        lv_obj_add_event_cb(btn, on_radio_pick, LV_EVENT_CLICKED, (void*)(intptr_t)i);
    }

    lv_obj_t *back = lv_btn_create(screenRadio);
    lv_obj_remove_style_all(back);
    lv_obj_add_style(back, &st_btn, 0);
    lv_obj_add_style(back, &st_btn_pressed, LV_STATE_PRESSED);
    lv_obj_set_size(back, 88, 28);
    lv_obj_align(back, LV_ALIGN_BOTTOM_LEFT, 8, -6);
    lv_obj_t *bl = lv_label_create(back);
    lv_label_set_text(bl, LV_SYMBOL_LEFT " BACK");
    lv_obj_center(bl);
    lv_obj_add_event_cb(back, on_back_to_sources, LV_EVENT_CLICKED, NULL);

    lv_obj_clear_flag(screenRadio, LV_OBJ_FLAG_SCROLLABLE);
}

// ===========================================================================
//                           T T S   S C R E E N
// ===========================================================================
typedef struct {
    const char *label;
    const char *text;
    const char *lang;
} tts_entry_t;

static const tts_entry_t tts_phrases[] = {
    { "Greeting",       "Welcome to cheap yellow display",            "en" },
    { "Stock audio",    "Stock audio system, no modification",        "en" },
    { "I2S mod",        "Audio I2S modification installed",           "en" },
    { "PSRAM mod",      "PSRAM modification installed",               "en" },
    { "Time check",     "It is time to listen to some music",         "en" },
};
static const int tts_count = sizeof(tts_phrases) / sizeof(tts_phrases[0]);

static void build_tts(void)
{
    add_status_bar(screenTTS, &sbT_l, &sbT_r, &sbT_m);

    lv_obj_t *title = lv_label_create(screenTTS);
    lv_obj_set_style_text_color(title, COL_PINK, 0);
    lv_obj_set_style_text_font(title, &UbuntuCond14, 0);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 8, 30);
    lv_label_set_text(title, "SPEECH SYNTHESIS");

    lv_obj_t *list = lv_list_create(screenTTS);
    lv_obj_set_size(list, 304, 148);
    lv_obj_align(list, LV_ALIGN_TOP_LEFT, 8, 48);
    lv_obj_set_style_bg_color(list, COL_BG, 0);
    lv_obj_set_style_border_color(list, COL_AMBER_DIM, 0);
    lv_obj_set_style_border_width(list, 1, 0);
    lv_obj_set_style_pad_all(list, 2, 0);
    lv_obj_set_style_radius(list, 0, 0);

    const lv_color_t t_accents[] = { COL_PINK, COL_PURPLE, COL_CYAN, COL_LIME, COL_YELLOW };
    for (int i = 0; i < tts_count; i++) {
        lv_obj_t *btn = lv_list_add_btn(list, LV_SYMBOL_KEYBOARD, tts_phrases[i].label);
        lv_color_t a = t_accents[i % 5];
        lv_obj_set_style_bg_color(btn, i & 1 ? COL_PANEL : COL_BG, 0);
        lv_obj_set_style_text_color(btn, a, 0);
        lv_obj_set_style_text_font(btn, &lv_font_montserrat_14, 0);
        lv_obj_set_style_border_color(btn, a, 0);
        lv_obj_set_style_border_side(btn, LV_BORDER_SIDE_BOTTOM, 0);
        lv_obj_set_style_border_width(btn, 1, 0);
        lv_obj_add_event_cb(btn, on_tts_pick, LV_EVENT_CLICKED, (void*)(intptr_t)i);
    }

    lv_obj_t *back = lv_btn_create(screenTTS);
    lv_obj_remove_style_all(back);
    lv_obj_add_style(back, &st_btn, 0);
    lv_obj_add_style(back, &st_btn_pressed, LV_STATE_PRESSED);
    lv_obj_set_size(back, 88, 28);
    lv_obj_align(back, LV_ALIGN_BOTTOM_LEFT, 8, -6);
    lv_obj_t *bl = lv_label_create(back);
    lv_label_set_text(bl, LV_SYMBOL_LEFT " BACK");
    lv_obj_center(bl);
    lv_obj_add_event_cb(back, on_back_to_sources, LV_EVENT_CLICKED, NULL);

    lv_obj_clear_flag(screenTTS, LV_OBJ_FLAG_SCROLLABLE);
}

// ===========================================================================
//                      S E T T I N G S / D I A G   S C R E E N
// ===========================================================================
static lv_obj_t *lblSysinfo;
static lv_obj_t *lblLdrLive;

static void sysinfo_text(char *buf, size_t n)
{
    esp_chip_info_t chip;
    esp_chip_info(&chip);
    const char *radio = (chip.features & CHIP_FEATURE_BT)
                      ? ((chip.features & CHIP_FEATURE_BLE) ? "BT/BLE" : "BT")
                      : ((chip.features & CHIP_FEATURE_BLE) ? "BLE" : "---");
    const char *fl    = (chip.features & CHIP_FEATURE_EMB_FLASH) ? "emb" : "ext";

    const char *mods =
#if defined(USE_I2S_DAC) && defined(BOARD_HAS_PSRAM)
        "PSRAM + I2S DAC";
#elif defined(USE_I2S_DAC)
        "I2S DAC";
#elif defined(BOARD_HAS_PSRAM)
        "PSRAM";
#else
        "stock";
#endif

    IPAddress ip = WiFi.localIP();
    snprintf(buf, n,
        "chip   : ESP32 r%d  %d core, %s\n"
        "flash  : %dMB %s\n"
        "heap   : %u / %u KB free\n"
        "psram  : %u / %u KB free\n"
        "wifi   : %s\n"
        "ip     : %d.%d.%d.%d\n"
        "mods   : %s",
        chip.revision, chip.cores, radio,
        (int)(spi_flash_get_chip_size() / (1024 * 1024)), fl,
        (unsigned)(ESP.getFreeHeap() / 1024), (unsigned)(ESP.getHeapSize() / 1024),
        (unsigned)(ESP.getFreePsram() / 1024), (unsigned)(ESP.getPsramSize() / 1024),
        wifiManager.getWLStatusString(),
        ip[0], ip[1], ip[2], ip[3],
        mods);
}

static void build_settings(void)
{
    add_status_bar(screenSettings, &sbX_l, &sbX_r, &sbX_m);

    lv_obj_t *title = lv_label_create(screenSettings);
    lv_obj_set_style_text_color(title, COL_YELLOW, 0);
    lv_obj_set_style_text_font(title, &UbuntuCond14, 0);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 8, 30);
    lv_label_set_text(title, "DIAGNOSTICS");

    lv_obj_t *panel = lv_obj_create(screenSettings);
    lv_obj_remove_style_all(panel);
    lv_obj_add_style(panel, &st_panel, 0);
    lv_obj_set_size(panel, 304, 116);
    lv_obj_align(panel, LV_ALIGN_TOP_LEFT, 8, 50);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);

    lblSysinfo = lv_label_create(panel);
    lv_label_set_recolor(lblSysinfo, true);
    lv_obj_set_style_text_color(lblSysinfo, COL_TXT, 0);
    lv_obj_set_style_text_font(lblSysinfo, &UbuntuCond14, 0);
    lv_obj_align(lblSysinfo, LV_ALIGN_TOP_LEFT, 0, 0);
    sysinfo_text(s_statusBuf, sizeof(s_statusBuf));
    lv_label_set_text(lblSysinfo, s_statusBuf);

    lblLdrLive = lv_label_create(screenSettings);
    lv_obj_set_style_text_color(lblLdrLive, COL_AMBER, 0);
    lv_obj_set_style_text_font(lblLdrLive, &UbuntuCond14, 0);
    lv_obj_align(lblLdrLive, LV_ALIGN_TOP_LEFT, 8, 170);
    lv_label_set_text(lblLdrLive, "LDR: ----   day/night: --");

    lv_obj_t *back = lv_btn_create(screenSettings);
    lv_obj_remove_style_all(back);
    lv_obj_add_style(back, &st_btn, 0);
    lv_obj_add_style(back, &st_btn_pressed, LV_STATE_PRESSED);
    lv_obj_set_size(back, 88, 28);
    lv_obj_align(back, LV_ALIGN_BOTTOM_LEFT, 8, -6);
    lv_obj_t *bl = lv_label_create(back);
    lv_label_set_text(bl, LV_SYMBOL_LEFT " PLAYER");
    lv_obj_center(bl);
    lv_obj_add_event_cb(back, on_back_to_player, LV_EVENT_CLICKED, NULL);

    lv_obj_t *wifi = lv_btn_create(screenSettings);
    lv_obj_remove_style_all(wifi);
    lv_obj_add_style(wifi, &st_btn, 0);
    lv_obj_add_style(wifi, &st_btn_pressed, LV_STATE_PRESSED);
    lv_obj_set_size(wifi, 120, 28);
    lv_obj_align(wifi, LV_ALIGN_BOTTOM_RIGHT, -8, -6);
    lv_obj_t *wl = lv_label_create(wifi);
    lv_label_set_text(wl, LV_SYMBOL_WIFI " RESET WIFI");
    lv_obj_center(wl);
    lv_obj_add_event_cb(wifi, on_settings_action, LV_EVENT_CLICKED, (void*)(intptr_t)0);

    lv_obj_clear_flag(screenSettings, LV_OBJ_FLAG_SCROLLABLE);
}

// ===========================================================================
//                         H E L P E R S
// ===========================================================================
static void extract_basename(const char *path, char *out, size_t n)
{
    const char *slash = strrchr(path, '/');
    if (slash) slash++; else slash = path;
    strncpy(out, slash, n - 1);
    out[n - 1] = 0;
    // Strip extension
    char *dot = strrchr(out, '.');
    if (dot) *dot = 0;
}

static void extract_folder(const char *path, char *out, size_t n)
{
    strncpy(out, path, n - 1);
    out[n - 1] = 0;
    char *slash = strrchr(out, '/');
    if (slash) *slash = 0;
    // Get last component
    const char *base = strrchr(path, '/');
    if (base) base++; else base = path;
    // If folder is /music or / just show path, otherwise show folder name
    if (strcmp(out, "/music") == 0 || strcmp(out, "/") == 0) {
        // Show as-is
    }
}

static void set_track(const char *name, bool playing)
{
    if (name) {
        strncpy(s_track, name, sizeof(s_track) - 1);
        s_track[sizeof(s_track) - 1] = 0;
    } else {
        s_track[0] = 0;
    }
    s_playing = playing;
    if (lblTrack) {
        if (s_track[0]) {
            char basename[64];
            extract_basename(s_track, basename, sizeof(basename));
            lv_label_set_text(lblTrack, basename);
        } else {
            lv_label_set_text(lblTrack, "[ STANDBY ]");
        }
    }
    // Update folder label
    if (lblFolder) {
        if (s_track[0]) {
            char folder[PATH_LEN];
            extract_folder(s_track, folder, sizeof(folder));
            // Show just the last folder component
            const char *last = strrchr(folder, '/');
            if (last && last[1]) last++; else last = folder;
            lv_label_set_text_fmt(lblFolder, LV_SYMBOL_DIRECTORY " %s", last);
        } else {
            lv_label_set_text(lblFolder, "");
        }
    }
    update_play_pause_btn();
}

static void update_play_pause_btn(void)
{
    if (!lblPlayPause) return;
    if (s_playing)
        lv_label_set_text(lblPlayPause, LV_SYMBOL_PAUSE);
    else
        lv_label_set_text(lblPlayPause, LV_SYMBOL_PLAY);
}

static void refresh_volume_label(int pct)
{
    if (lblVol) lv_label_set_text_fmt(lblVol, "%d%%", pct);
}

// ===========================================================================
//                         T I M E R   C A L L B A C K S
// ===========================================================================
static void cb_vu(lv_timer_t *t)
{
    if (lv_scr_act() != screenPlayer) return;
    uint32_t vu = audioGetRMS();
    uint8_t r = (vu >> 16) & 0xFF;
    uint8_t l = vu & 0xFF;
    lv_bar_set_value(vuL, l, LV_ANIM_OFF);
    lv_bar_set_value(vuR, r, LV_ANIM_OFF);
}

static void cb_status(lv_timer_t *t)
{
    refresh_status_bar();

    bool now = audioIsPlaying();
    if (now != s_playing) {
        s_playing = now;
        update_play_pause_btn();
        if (lblNowPlaying) {
            lv_label_set_text(lblNowPlaying,
                now ? LV_SYMBOL_PAUSE " NOW PLAYING" : LV_SYMBOL_PLAY " STOPPED");
            lv_obj_set_style_text_color(lblNowPlaying, now ? COL_LIME : COL_TXT_DIM, 0);
        }
    }
}

static void cb_settings(lv_timer_t *t)
{
    if (lv_scr_act() != screenSettings) return;
    sysinfo_text(s_statusBuf, sizeof(s_statusBuf));
    lv_label_set_text(lblSysinfo, s_statusBuf);
    lv_label_set_text_fmt(lblLdrLive, "LDR: %4d   %s",
                          (int)ldr.get(), ldr.isDark() ? "night" : "day");
}

static void cb_disc(lv_timer_t *t)
{
    if (!s_playing) return;
    if (lv_scr_act() != screenPlayer) return;
    disc_angle = (disc_angle + 120) % 3600;
    if (imgDisc) lv_img_set_angle(imgDisc, disc_angle);
}

// ===========================================================================
//                         E V E N T   H A N D L E R S
// ===========================================================================
static void on_vol_change(lv_event_t *e)
{
    int32_t val = lv_slider_get_value(volSlider);
    refresh_volume_label(val);
    audioSetVolume((val * 21) / 100);
}

static void on_transport(lv_event_t *e)
{
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    switch (idx) {
        case 0: // PREV
            if (s_pl_count > 0 && s_pl_idx > 0) {
                playlist_play_idx(s_pl_idx - 1);
            }
            break;
        case 1: // PLAY / PAUSE toggle
            if (s_playing) {
                // Currently playing -> stop
                audioStopSong();
                s_playing = false;
                update_play_pause_btn();
                if (lblNowPlaying) {
                    lv_label_set_text(lblNowPlaying, LV_SYMBOL_PLAY " STOPPED");
                    lv_obj_set_style_text_color(lblNowPlaying, COL_TXT_DIM, 0);
                }
            } else {
                // Stopped -> play current track (or re-trigger)
                if (s_track[0]) {
                    if (s_pl_idx >= 0) {
                        playlist_play_idx(s_pl_idx);
                    } else {
                        audioConnecttoSD(s_track);
                        set_track(s_track, true);
                    }
                }
            }
            break;
        case 2: // NEXT
            if (s_pl_count > 0 && s_pl_idx < s_pl_count - 1) {
                playlist_play_idx(s_pl_idx + 1);
            }
            break;
        case 3: // FOLDER - jump to browser in current playlist dir
            if (s_pl_dir[0]) {
                snprintf(browser_cwd, sizeof(browser_cwd), "%s", s_pl_dir);
            }
            browser_populate(browserList);
            lv_scr_load_anim(screenBrowser, LV_SCR_LOAD_ANIM_MOVE_LEFT, 200, 0, false);
            refresh_status_bar();
            break;
        case 4: // SOURCES
            lv_scr_load_anim(screenSources, LV_SCR_LOAD_ANIM_MOVE_LEFT, 200, 0, false);
            refresh_status_bar();
            break;
    }
}

static void on_source_pick(lv_event_t *e)
{
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    switch (idx) {
        case 0:
            browser_populate(browserList);
            lv_scr_load_anim(screenBrowser, LV_SCR_LOAD_ANIM_MOVE_LEFT, 200, 0, false);
            break;
        case 1:
            lv_scr_load_anim(screenRadio, LV_SCR_LOAD_ANIM_MOVE_LEFT, 200, 0, false);
            break;
        case 2:
            lv_scr_load_anim(screenTTS, LV_SCR_LOAD_ANIM_MOVE_LEFT, 200, 0, false);
            break;
        case 3:
            lv_scr_load_anim(screenSettings, LV_SCR_LOAD_ANIM_MOVE_LEFT, 200, 0, false);
            break;
    }
    refresh_status_bar();
}

static void on_browser_pick(lv_event_t *e)
{
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (idx < 0 || idx >= browser_n_entries) return;
    const char *name = browser_entries[idx];

    if (browser_is_dir[idx]) {
        char next[BROWSER_PATH_LEN];
        if (strcmp(name, "..") == 0)  path_parent(next, sizeof(next), browser_cwd);
        else                           path_join(next, sizeof(next), browser_cwd, name);
        snprintf(browser_cwd, sizeof(browser_cwd), "%s", next);
        browser_populate(browserList);
        return;
    }

    // file: build full path, load playlist, play
    static char path[PATH_LEN + BROWSER_PATH_LEN];
    path_join(path, sizeof(path), browser_cwd, name);

    // Load playlist from this folder
    playlist_load(browser_cwd);
    // Find this file in the playlist
    for (int i = 0; i < s_pl_count; i++) {
        if (strcmp(s_pl_files[i], name) == 0) {
            s_pl_idx = i;
            break;
        }
    }

    audioConnecttoSD(path);
    set_track(path, true);
    lv_scr_load_anim(screenPlayer, LV_SCR_LOAD_ANIM_MOVE_RIGHT, 200, 0, false);
    refresh_status_bar();
}

static void on_browser_refresh(lv_event_t *e)
{
    browser_populate(browserList);
}

static void on_radio_pick(lv_event_t *e)
{
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (idx < 0 || idx >= radio_count) return;
    // Radio streams don't have a playlist folder
    s_pl_count = 0;
    s_pl_idx = -1;
    s_pl_dir[0] = 0;
    audioConnecttohost(radio_presets[idx].url);
    set_track(radio_presets[idx].label, true);
    lv_scr_load_anim(screenPlayer, LV_SCR_LOAD_ANIM_MOVE_RIGHT, 200, 0, false);
    refresh_status_bar();
}

static void on_tts_pick(lv_event_t *e)
{
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (idx < 0 || idx >= tts_count) return;
    s_pl_count = 0;
    s_pl_idx = -1;
    s_pl_dir[0] = 0;
    audioConnecttoSpeech(tts_phrases[idx].text, tts_phrases[idx].lang);
    set_track(tts_phrases[idx].label, true);
    lv_scr_load_anim(screenPlayer, LV_SCR_LOAD_ANIM_MOVE_RIGHT, 200, 0, false);
    refresh_status_bar();
}

static void on_back_to_player(lv_event_t *e)
{
    lv_scr_load_anim(screenPlayer, LV_SCR_LOAD_ANIM_MOVE_RIGHT, 200, 0, false);
    refresh_status_bar();
}

static void on_back_to_sources(lv_event_t *e)
{
    lv_scr_load_anim(screenSources, LV_SCR_LOAD_ANIM_MOVE_RIGHT, 200, 0, false);
    refresh_status_bar();
}

static void on_settings_action(lv_event_t *e)
{
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    switch (idx) {
        case 0:
            wifiManager.resetSettings();
            ESP.restart();
            break;
    }
}

// ===========================================================================
//        Backwards-compat: kept because CYD28_audio.h declares it.
// ===========================================================================
void setVuMeters(uint32_t vuRL)
{
    uint8_t r = (vuRL >> 16) & 0xFF;
    uint8_t l = vuRL & 0xFF;
    if (vuL) lv_bar_set_value(vuL, l, LV_ANIM_OFF);
    if (vuR) lv_bar_set_value(vuR, r, LV_ANIM_OFF);
}

void audio_info(const char *s) { Serial.printf("%s\r\n", s); }
void audio_eof_mp3(const char *info)
{
    Serial.print("eof_mp3     ");
    Serial.println(info);
}