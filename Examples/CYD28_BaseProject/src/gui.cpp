/**
 * @file gui.cpp
 * @brief Retro audio-player UI for the CYD 2.8" board.
 *
 *   Screens:
 *     - screenPlayer    : disc + vertical VU, track info, volume, transport
 *     - screenSources   : SD files / Settings picker
 *     - screenBrowser   : list of audio files on the SD card
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

// ---------------- neon palette: green, pink, black, white, gray ---------------
#define COL_BG          lv_color_hex(0x000000)
#define COL_BG_BAR      lv_color_hex(0x000000)
#define COL_PANEL       lv_color_hex(0x111111)
#define COL_PANEL_BRD   lv_color_hex(0x333333)
#define COL_TXT         lv_color_hex(0xffffff)
#define COL_TXT_DIM     lv_color_hex(0x808080)
#define COL_GREEN       lv_color_hex(0x39ff14)
#define COL_PINK        lv_color_hex(0xff1493)
#define COL_GRAY        lv_color_hex(0x555555)

// ---------------- screens --------------------------------------------------
static lv_obj_t *screenPlayer;
static lv_obj_t *screenSources;
static lv_obj_t *screenBrowser;
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
static lv_obj_t *volTag;
static lv_obj_t *btnPlayPause;
static lv_obj_t *lblPlayPause;
static lv_obj_t *barProgress;
static lv_obj_t *lblTimeElapsed;
static lv_obj_t *lblTimeDuration;

// ---------------- spinning disc --------------------------------------------
LV_IMG_DECLARE(disc_img);
static lv_obj_t *imgDisc;
static int32_t   disc_angle = 0;

// ---------------- timers ---------------------------------------------------
static lv_timer_t *tmrVu;
static lv_timer_t *tmrStatus;
static lv_timer_t *tmrSettings;
static lv_timer_t *tmrDisc;
static lv_timer_t *tmrPlistLoad;

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
static uint32_t s_frame_cnt  = 0;
static uint32_t s_fps_tick   = 0;
static uint32_t s_fps_val    = 0;

// ---- play mode: 0=loop all, 1=shuffle, 2=repeat one ----
typedef enum { PM_LOOP_ALL = 0, PM_SHUFFLE = 1, PM_REPEAT_ONE = 2 } play_mode_t;
static play_mode_t s_play_mode = PM_LOOP_ALL;
static lv_obj_t   *btnPlayMode = NULL;
static lv_obj_t   *lblPlayMode = NULL;
static volatile bool s_track_ended = false;  // set by audio callback, consumed by LVGL timer
static bool s_vol_visible = false;

// ---- saved playlists ----
#define PL_SAVED_MAX  4
#define PL_ENTRY_MAX  32
#define PL_PATH_LEN   128

struct saved_playlist_t {
    char paths[PL_ENTRY_MAX][PL_PATH_LEN];
    int  count;
};
static saved_playlist_t s_plists[PL_SAVED_MAX];
static lv_obj_t *btnPlIcon[PL_SAVED_MAX];   // player screen playlist icons
static lv_obj_t *lblPlCount[PL_SAVED_MAX];   // source screen count labels
static bool s_pl_from_saved = false;         // true when playing from saved playlist
static char s_pl_fullpaths[PLAYLIST_MAX][PL_PATH_LEN]; // full paths for saved-playlist mode

static const char *pl_names[PL_SAVED_MAX] = { "Favorites", "Mix", "X-List", "Circle" };
static const char *pl_syms[PL_SAVED_MAX]   = { "A", "B", "X", "Y" };
static const lv_color_t pl_colors[PL_SAVED_MAX] = { COL_PINK, COL_GREEN, COL_TXT, lv_color_hex(0x4488ff) };

// ---------------- forward decls --------------------------------------------
static void build_player(void);
static void build_sources(void);
static void build_browser(void);
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

static void fmt_time(uint32_t secs, char *buf, size_t n);

static void on_vol_change(lv_event_t *e);
static void on_transport(lv_event_t *e);
static void on_source_pick(lv_event_t *e);
static void on_browser_pick(lv_event_t *e);
static void on_back_to_player(lv_event_t *e);
static void on_back_to_sources(lv_event_t *e);
static void on_settings_action(lv_event_t *e);
static void on_browser_refresh(lv_event_t *e);
static void on_play_mode(lv_event_t *e);
static void on_source_tap(lv_event_t *e);
static void advance_track(void);
static void on_plist_source_pick(lv_event_t *e);
static void on_plist_add_track(lv_event_t *e);
static void plists_refresh_player_icons(void);
static void plists_refresh_source_labels(void);
static void plist_play(int pl_idx);
static bool plist_save(int pl_idx);
static void plists_load_all(void);
static void cb_plist_load(lv_timer_t *t);

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
    lv_style_set_bg_color(&st_vu_bg, COL_BG);
    lv_style_set_bg_opa(&st_vu_bg, LV_OPA_COVER);
    lv_style_set_border_color(&st_vu_bg, COL_GRAY);
    lv_style_set_border_width(&st_vu_bg, 1);
    lv_style_set_radius(&st_vu_bg, 0);

    lv_style_init(&st_vu_indic);
    lv_style_set_bg_opa(&st_vu_indic, LV_OPA_COVER);
    lv_style_set_bg_color(&st_vu_indic, COL_GREEN);
    lv_style_set_radius(&st_vu_indic, 0);

    lv_style_init(&st_btn);
    lv_style_set_bg_color(&st_btn, COL_BG);
    lv_style_set_bg_opa(&st_btn, LV_OPA_COVER);
    lv_style_set_border_color(&st_btn, COL_GREEN);
    lv_style_set_border_width(&st_btn, 1);
    lv_style_set_radius(&st_btn, 2);
    lv_style_set_text_color(&st_btn, COL_GREEN);
    lv_style_set_text_font(&st_btn, &lv_font_montserrat_14);
    lv_style_set_pad_all(&st_btn, 4);

    lv_style_init(&st_btn_pressed);
    lv_style_set_bg_color(&st_btn_pressed, COL_GREEN);
    lv_style_set_text_color(&st_btn_pressed, lv_color_hex(0x000000));

    lv_style_init(&st_chip);
    lv_style_set_bg_color(&st_chip, COL_BG);
    lv_style_set_bg_opa(&st_chip, LV_OPA_COVER);
    lv_style_set_border_color(&st_chip, COL_GRAY);
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
    screenSettings = lv_obj_create(NULL);

    lv_obj_add_style(screenPlayer,   &st_scr, 0);
    lv_obj_add_style(screenSources,  &st_scr, 0);
    lv_obj_add_style(screenBrowser,  &st_scr, 0);
    lv_obj_add_style(screenSettings, &st_scr, 0);

    build_player();
    build_sources();
    build_browser();
    build_settings();

    tmrVu       = lv_timer_create(cb_vu,       60,   NULL);
    tmrStatus   = lv_timer_create(cb_status,   1500, NULL);
    tmrSettings = lv_timer_create(cb_settings, 2000, NULL);
    tmrDisc     = lv_timer_create(cb_disc,     50,   NULL);

    tmrPlistLoad = lv_timer_create(cb_plist_load, 2000, NULL);
    lv_timer_set_repeat_count(tmrPlistLoad, 1);

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
    lv_obj_set_style_bg_color(bar, COL_BG, 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(bar, COL_GRAY, 0);
    lv_obj_set_style_border_side(bar, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_width(bar, 1, 0);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);

    *lblLeft = lv_label_create(bar);
    lv_obj_set_style_text_color(*lblLeft, COL_TXT, 0);
    lv_obj_set_style_text_font(*lblLeft, &lv_font_montserrat_14, 0);
    lv_obj_align(*lblLeft, LV_ALIGN_LEFT_MID, 6, 0);
    lv_label_set_text(*lblLeft, LV_SYMBOL_WIFI " ---");

    *lblMid = lv_label_create(bar);
    lv_obj_set_style_text_color(*lblMid, COL_TXT_DIM, 0);
    lv_obj_set_style_text_font(*lblMid, &UbuntuCond14, 0);
    lv_obj_align(*lblMid, LV_ALIGN_CENTER, 0, 0);
    lv_label_set_text(*lblMid, "");

    *lblRight = lv_label_create(bar);
    lv_obj_set_style_text_color(*lblRight, COL_TXT, 0);
    lv_obj_set_style_text_font(*lblRight, &UbuntuCond14, 0);
    lv_obj_align(*lblRight, LV_ALIGN_RIGHT_MID, -6, 0);
    lv_label_set_text(*lblRight, "");
}

// ---- per-screen status bar handles ----
static lv_obj_t *sbP_l, *sbP_r, *sbP_m;
static lv_obj_t *sbS_l, *sbS_r, *sbS_m;
static lv_obj_t *sbB_l, *sbB_r, *sbB_m;
static lv_obj_t *sbX_l, *sbX_r, *sbX_m;

static void refresh_status_bar(void)
{
    char left[40];
    char mid[40];

    if (WiFi.status() == WL_CONNECTED) {
        IPAddress ip = WiFi.localIP();
        snprintf(left, sizeof(left), LV_SYMBOL_WIFI " %d.%d.%d.%d",
                 ip[0], ip[1], ip[2], ip[3]);
    } else {
        snprintf(left, sizeof(left), LV_SYMBOL_WIFI " offline");
    }
    snprintf(mid, sizeof(mid), "%u fps  %luk free", s_fps_val, (unsigned)(ESP.getFreeHeap() / 1024));

    lv_obj_t *active = lv_scr_act();
    if (active == screenPlayer)        { lv_label_set_text(sbP_l, left); lv_label_set_text(sbP_m, mid); }
    else if (active == screenSources)  { lv_label_set_text(sbS_l, left); lv_label_set_text(sbS_m, mid); }
    else if (active == screenBrowser)  { lv_label_set_text(sbB_l, left); lv_label_set_text(sbB_m, mid); }
    else if (active == screenSettings) { lv_label_set_text(sbX_l, left); lv_label_set_text(sbX_m, mid); }
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
    s_pl_from_saved = false;
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
    if (s_pl_from_saved) {
        snprintf(path, sizeof(path), "%s", s_pl_fullpaths[idx]);
    } else {
        if (strcmp(s_pl_dir, "/") == 0 || s_pl_dir[0] == 0)
            snprintf(path, sizeof(path), "/%s", s_pl_files[idx]);
        else
            snprintf(path, sizeof(path), "%s/%s", s_pl_dir, s_pl_files[idx]);
    }
    audioConnecttoSD(path);
    set_track(path, true);
    plists_refresh_player_icons();
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
    lv_obj_set_size(b, 54, 34);
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

    // ---- "..." button top-right -> opens Sources screen ----
    lv_obj_t *srcTap = lv_btn_create(screenPlayer);
    lv_obj_remove_style_all(srcTap);
    lv_obj_set_size(srcTap, 40, 24);
    lv_obj_set_pos(srcTap, 278, 0);
    lv_obj_clear_flag(srcTap, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *srcLbl = lv_label_create(srcTap);
    lv_label_set_text(srcLbl, "::::");
    lv_obj_set_style_text_color(srcLbl, COL_TXT, 0);
    lv_obj_set_style_text_font(srcLbl, &lv_font_montserrat_14, 0);
    lv_obj_center(srcLbl);
    lv_obj_add_event_cb(srcTap, on_source_tap, LV_EVENT_CLICKED, NULL);

    // ---- Spinning disc (top-left) ----
    imgDisc = lv_img_create(screenPlayer);
    lv_img_set_src(imgDisc, &disc_img);
    lv_obj_set_pos(imgDisc, 4, 40);
    lv_img_set_pivot(imgDisc, 40, 40);
    lv_img_set_antialias(imgDisc, false);
    lv_img_set_angle(imgDisc, 0);

    // ---- VU labels (L / R) above bars, right of disc ----
    lv_obj_t *vuLlbl = lv_label_create(screenPlayer);
    lv_obj_set_style_text_color(vuLlbl, COL_GREEN, 0);
    lv_obj_set_style_text_font(vuLlbl, &lv_font_montserrat_14, 0);
    lv_obj_set_pos(vuLlbl, 94, 39);
    lv_label_set_text(vuLlbl, "L");

    lv_obj_t *vuRlbl = lv_label_create(screenPlayer);
    lv_obj_set_style_text_color(vuRlbl, COL_PINK, 0);
    lv_obj_set_style_text_font(vuRlbl, &lv_font_montserrat_14, 0);
    lv_obj_set_pos(vuRlbl, 110, 39);
    lv_label_set_text(vuRlbl, "R");

    // ---- Vertical VU bars (right of disc) ----
    vuL = lv_bar_create(screenPlayer);
    lv_obj_remove_style_all(vuL);
    lv_obj_add_style(vuL, &st_vu_bg, LV_PART_MAIN);
    lv_obj_add_style(vuL, &st_vu_indic, LV_PART_INDICATOR);
    lv_obj_set_size(vuL, 14, 78);
    lv_obj_set_pos(vuL, 92, 54);
    lv_obj_set_style_pad_all(vuL, 2, LV_PART_MAIN);
    lv_bar_set_range(vuL, 0, 100);
    lv_bar_set_value(vuL, 0, LV_ANIM_OFF);

    vuR = lv_bar_create(screenPlayer);
    lv_obj_remove_style_all(vuR);
    lv_obj_add_style(vuR, &st_vu_bg, LV_PART_MAIN);
    lv_obj_add_style(vuR, &st_vu_indic, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(vuR, COL_PINK, LV_PART_INDICATOR);
    lv_obj_set_size(vuR, 14, 78);
    lv_obj_set_pos(vuR, 108, 54);
    lv_obj_set_style_pad_all(vuR, 2, LV_PART_MAIN);
    lv_bar_set_range(vuR, 0, 100);
    lv_bar_set_value(vuR, 0, LV_ANIM_OFF);

    // ---- Track info area (right of disc+VU) ----
    lblNowPlaying = lv_label_create(screenPlayer);
    lv_obj_set_style_text_color(lblNowPlaying, COL_PINK, 0);
    lv_obj_set_style_text_font(lblNowPlaying, &lv_font_montserrat_14, 0);
    lv_obj_set_pos(lblNowPlaying, 128, 50);
    lv_label_set_text(lblNowPlaying, LV_SYMBOL_PLAY " NOW PLAYING");

    // Track name (large, scrolling)
    lblTrack = lv_label_create(screenPlayer);
    lv_obj_set_style_text_color(lblTrack, COL_GREEN, 0);
    lv_obj_set_style_text_font(lblTrack, &UbuntuCond36, 0);
    lv_obj_set_width(lblTrack, 184);
    lv_label_set_long_mode(lblTrack, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_pos(lblTrack, 128, 70);
    lv_label_set_text(lblTrack, "[ STANDBY ]");

    // Folder name (no rectangle icon, just text)
    lblFolder = lv_label_create(screenPlayer);
    lv_obj_set_style_text_color(lblFolder, COL_TXT_DIM, 0);
    lv_obj_set_style_text_font(lblFolder, &UbuntuCond14, 0);
    lv_obj_set_width(lblFolder, 140);
    lv_label_set_long_mode(lblFolder, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_pos(lblFolder, 128, 108);
    lv_label_set_text(lblFolder, "");

    // ---- playlist icon buttons (below folder label) ----
    for (int i = 0; i < PL_SAVED_MAX; i++) {
        lv_obj_t *btn = lv_btn_create(screenPlayer);
        lv_obj_remove_style_all(btn);
        lv_obj_set_size(btn, 30, 30);
        lv_obj_set_pos(btn, 128 + i * 38, 130);
        lv_obj_set_style_bg_opa(btn, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(btn, 0, 0);
        lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t *lbl = lv_label_create(btn);
        lv_label_set_text(lbl, pl_syms[i]);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(lbl, pl_colors[i], 0);
        lv_obj_align(lbl, LV_ALIGN_TOP_MID, 0, 0);

        lv_obj_t *line = lv_obj_create(btn);
        lv_obj_remove_style_all(line);
        lv_obj_set_size(line, 20, 2);
        lv_obj_align(line, LV_ALIGN_BOTTOM_MID, 0, -8);
        lv_obj_set_style_bg_color(line, pl_colors[i], 0);
        lv_obj_set_style_bg_opa(line, LV_OPA_TRANSP, 0);
        lv_obj_set_style_radius(line, 0, 0);
        lv_obj_clear_flag(line, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(line, LV_OBJ_FLAG_HIDDEN);

        lv_obj_add_event_cb(btn, on_plist_add_track, LV_EVENT_CLICKED, (void*)(intptr_t)i);
        btnPlIcon[i] = btn;
    }

    // ---- Volume slider (vertical, right side, hidden by default) ----
    volTag = lv_label_create(screenPlayer);
    lv_obj_set_style_text_color(volTag, COL_TXT, 0);
    lv_obj_set_style_text_font(volTag, &lv_font_montserrat_14, 0);
    lv_obj_set_pos(volTag, 308, 30);
    lv_label_set_text(volTag, LV_SYMBOL_VOLUME_MAX);

    volSlider = lv_slider_create(screenPlayer);
    lv_obj_set_size(volSlider, 8, 120);
    lv_obj_set_pos(volSlider, 306, 48);
    lv_slider_set_range(volSlider, 0, 100);
    lv_obj_set_style_bg_color(volSlider, COL_PANEL, LV_PART_MAIN);
    lv_obj_set_style_border_color(volSlider, COL_GRAY, LV_PART_MAIN);
    lv_obj_set_style_border_width(volSlider, 1, LV_PART_MAIN);
    lv_obj_set_style_bg_color(volSlider, COL_PINK, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(volSlider, COL_GREEN, LV_PART_KNOB);
    lv_obj_set_style_pad_all(volSlider, 4, LV_PART_KNOB);
    lv_obj_add_event_cb(volSlider, on_vol_change, LV_EVENT_VALUE_CHANGED, NULL);
    // Hide volume by default
    lv_obj_add_flag(volTag, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(volSlider, LV_OBJ_FLAG_HIDDEN);

    lblVol = lv_label_create(screenPlayer);
    lv_obj_set_style_text_color(lblVol, COL_TXT, 0);
    lv_obj_set_style_text_font(lblVol, &lv_font_montserrat_14, 0);
    lv_obj_set_pos(lblVol, 297, 170);
    lv_label_set_text(lblVol, "---");
    lv_obj_add_flag(lblVol, LV_OBJ_FLAG_HIDDEN);

    int pct = audioGetVolumePerCent();
    // Override to 50% on boot
    audioSetVolume(11);
    pct = 50;
    lv_slider_set_value(volSlider, pct, LV_ANIM_OFF);
    refresh_volume_label(pct);

    // ---- Time labels above progress bar ----
    lblTimeElapsed = lv_label_create(screenPlayer);
    lv_obj_set_style_text_color(lblTimeElapsed, COL_TXT_DIM, 0);
    lv_obj_set_style_text_font(lblTimeElapsed, &UbuntuCond14, 0);
    lv_obj_set_pos(lblTimeElapsed, 4, 168);
    lv_label_set_text(lblTimeElapsed, "0:00");

    lblTimeDuration = lv_label_create(screenPlayer);
    lv_obj_set_style_text_color(lblTimeDuration, COL_TXT_DIM, 0);
    lv_obj_set_style_text_font(lblTimeDuration, &UbuntuCond14, 0);
    lv_obj_set_pos(lblTimeDuration, 276, 168);
    lv_label_set_text(lblTimeDuration, "0:00");

    // ---- Transport bar with integrated progress ----
    lv_obj_t *tbar = lv_obj_create(screenPlayer);
    lv_obj_remove_style_all(tbar);
    lv_obj_set_size(tbar, 320, 62);
    lv_obj_set_pos(tbar, 0, 184);
    lv_obj_set_style_bg_color(tbar, COL_BG, 0);
    lv_obj_set_style_bg_opa(tbar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(tbar, 0, 0);
    lv_obj_clear_flag(tbar, LV_OBJ_FLAG_SCROLLABLE);

    // Progress bar: gray border box, green fill grows left to right
    barProgress = lv_bar_create(tbar);
    lv_obj_remove_style_all(barProgress);
    lv_obj_add_style(barProgress, &st_vu_bg, LV_PART_MAIN);
    lv_obj_set_style_pad_all(barProgress, 2, LV_PART_MAIN);
    lv_obj_set_style_border_side(barProgress, LV_BORDER_SIDE_BOTTOM, LV_PART_MAIN);
    lv_obj_set_size(barProgress, 310, 7);
    lv_obj_align(barProgress, LV_ALIGN_TOP_MID, 0, 2);
    lv_bar_set_range(barProgress, 0, 100);
    lv_bar_set_value(barProgress, 0, LV_ANIM_OFF);
    lv_obj_add_style(barProgress, &st_vu_indic, LV_PART_INDICATOR);

    // transport idx: 1=PREV, 2=PLAY/PAUSE, 3=NEXT, 4=FOLDER, 5=VOL (MODE has own handler)
    // Play mode button leftmost (cycles: loop all -> shuffle -> repeat one)
    btnPlayMode = lv_btn_create(tbar);
    lv_obj_remove_style_all(btnPlayMode);
    lv_obj_add_style(btnPlayMode, &st_btn, 0);
    lv_obj_add_style(btnPlayMode, &st_btn_pressed, LV_STATE_PRESSED);
    lv_obj_set_size(btnPlayMode, 34, 34);
    lv_obj_align(btnPlayMode, LV_ALIGN_LEFT_MID, 20, 0);
    lv_obj_set_style_radius(btnPlayMode, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_color(btnPlayMode, COL_GRAY, 0);
    lv_obj_set_style_text_color(btnPlayMode, COL_GRAY, 0);
    lv_obj_set_style_bg_color(btnPlayMode, COL_GRAY, LV_STATE_PRESSED);
    lblPlayMode = lv_label_create(btnPlayMode);
    lv_label_set_text(lblPlayMode, LV_SYMBOL_LOOP);
    lv_obj_set_style_text_font(lblPlayMode, &lv_font_montserrat_14, 0);
    lv_obj_center(lblPlayMode);
    lv_obj_add_event_cb(btnPlayMode, on_play_mode, LV_EVENT_CLICKED, NULL);

    make_transport_btn(tbar, LV_SYMBOL_PREV,    62,  1, COL_GRAY);
    make_transport_btn(tbar, LV_SYMBOL_NEXT,     153,  3, COL_GRAY);
    // Play/pause round button, overlapping both PREV and NEXT (created after so it draws on top)
    btnPlayPause = lv_btn_create(tbar);
    lv_obj_remove_style_all(btnPlayPause);
    lv_obj_add_style(btnPlayPause, &st_btn, 0);
    lv_obj_add_style(btnPlayPause, &st_btn_pressed, LV_STATE_PRESSED);
    lv_obj_set_size(btnPlayPause, 60, 60);
    lv_obj_align(btnPlayPause, LV_ALIGN_LEFT_MID, 105, 0);
    lv_obj_set_style_radius(btnPlayPause, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_color(btnPlayPause, COL_PINK, 0);
    lv_obj_set_style_text_color(btnPlayPause, COL_PINK, 0);
    lv_obj_set_style_bg_color(btnPlayPause, COL_PINK, LV_STATE_PRESSED);
    lblPlayPause = lv_label_create(btnPlayPause);
    lv_label_set_text(lblPlayPause, LV_SYMBOL_PLAY);
    lv_obj_set_style_text_font(lblPlayPause, &lv_font_montserrat_14, 0);
    lv_obj_center(lblPlayPause);
    lv_obj_add_event_cb(btnPlayPause, on_transport, LV_EVENT_CLICKED, (void*)(intptr_t)2);

    // FOLDER button as circle
    lv_obj_t *btnFolder = lv_btn_create(tbar);
    lv_obj_remove_style_all(btnFolder);
    lv_obj_add_style(btnFolder, &st_btn, 0);
    lv_obj_add_style(btnFolder, &st_btn_pressed, LV_STATE_PRESSED);
    lv_obj_set_size(btnFolder, 34, 34);
    lv_obj_align(btnFolder, LV_ALIGN_LEFT_MID, 217, 0);
    lv_obj_set_style_radius(btnFolder, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_color(btnFolder, COL_PINK, 0);
    lv_obj_set_style_text_color(btnFolder, COL_PINK, 0);
    lv_obj_set_style_bg_color(btnFolder, COL_PINK, LV_STATE_PRESSED);
    lv_obj_t *lblFolderBtn = lv_label_create(btnFolder);
    lv_label_set_text(lblFolderBtn, LV_SYMBOL_DIRECTORY);
    lv_obj_set_style_text_font(lblFolderBtn, &lv_font_montserrat_14, 0);
    lv_obj_center(lblFolderBtn);
    lv_obj_add_event_cb(btnFolder, on_transport, LV_EVENT_CLICKED, (void*)(intptr_t)4);

    // Volume toggle button (circle)
    lv_obj_t *btnVol = lv_btn_create(tbar);
    lv_obj_remove_style_all(btnVol);
    lv_obj_add_style(btnVol, &st_btn, 0);
    lv_obj_add_style(btnVol, &st_btn_pressed, LV_STATE_PRESSED);
    lv_obj_set_size(btnVol, 34, 34);
    lv_obj_align(btnVol, LV_ALIGN_LEFT_MID, 260, 0);
    lv_obj_set_style_radius(btnVol, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_color(btnVol, COL_PINK, 0);
    lv_obj_set_style_text_color(btnVol, COL_PINK, 0);
    lv_obj_set_style_bg_color(btnVol, COL_PINK, LV_STATE_PRESSED);
    lv_obj_t *lblVolBtn = lv_label_create(btnVol);
    lv_label_set_text(lblVolBtn, LV_SYMBOL_VOLUME_MAX);
    lv_obj_set_style_text_font(lblVolBtn, &lv_font_montserrat_14, 0);
    lv_obj_center(lblVolBtn);
    lv_obj_add_event_cb(btnVol, on_transport, LV_EVENT_CLICKED, (void*)(intptr_t)5);

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

    // ---- playlist rows ----
    for (int i = 0; i < PL_SAVED_MAX; i++) {
        lv_obj_t *row = lv_btn_create(screenSources);
        lv_obj_remove_style_all(row);
        lv_obj_add_style(row, &st_btn, 0);
        lv_obj_add_style(row, &st_btn_pressed, LV_STATE_PRESSED);
        lv_obj_set_size(row, 304, 24);
        lv_obj_set_pos(row, 8, 50 + i * 28);
        lv_obj_set_style_border_color(row, pl_colors[i], 0);
        lv_obj_set_style_border_width(row, 1, 0);
        lv_obj_set_style_radius(row, 4, 0);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t *icon = lv_label_create(row);
        lv_label_set_text(icon, pl_syms[i]);
        lv_obj_set_style_text_font(icon, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(icon, pl_colors[i], 0);
        lv_obj_align(icon, LV_ALIGN_LEFT_MID, 6, 0);

        lv_obj_t *lbl = lv_label_create(row);
        lv_label_set_text_fmt(lbl, "%s (%d)", pl_names[i], s_plists[i].count);
        lv_obj_set_style_text_font(lbl, &UbuntuCond14, 0);
        lv_obj_set_style_text_color(lbl, COL_TXT, 0);
        lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 28, 0);
        lblPlCount[i] = lbl;

        lv_obj_add_event_cb(row, on_plist_source_pick, LV_EVENT_CLICKED, (void*)(intptr_t)i);
    }

    make_big_btn(screenSources, LV_SYMBOL_SD_CARD,  "SD FILES",  8,   164, 148, 50, 0, on_source_pick, COL_GREEN);
    make_big_btn(screenSources, LV_SYMBOL_SETTINGS, "SETTINGS",  164, 164, 148, 50, 1, on_source_pick, COL_GREEN);

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

// ---- playlist JSON helpers ----
static int pl_json_parse(const char *json, int json_len, char out[][PL_PATH_LEN], int max_entries)
{
    int count = 0;
    int i = 0;
    while (i < json_len && json[i] != '[') i++;
    if (i >= json_len) return 0;
    i++; // skip '['
    while (i < json_len && count < max_entries) {
        while (i < json_len && (json[i]==' '||json[i]==','||json[i]=='\n'||json[i]=='\r'||json[i]=='\t')) i++;
        if (i >= json_len || json[i] == ']') break;
        if (json[i] != '"') { i++; continue; }
        i++; // skip opening quote
        int p = 0;
        while (i < json_len && json[i] != '"' && p < PL_PATH_LEN - 1) {
            if (json[i] == '\\' && i + 1 < json_len) {
                i++;
                if (json[i] == '"' || json[i] == '\\') { out[count][p++] = json[i++]; }
                else if (json[i] == 'n') { out[count][p++] = '\n'; i++; }
                else { out[count][p++] = json[i++]; }
            } else {
                out[count][p++] = json[i++];
            }
        }
        out[count][p] = 0;
        if (p > 0) count++;
        if (i < json_len && json[i] == '"') i++;
    }
    return count;
}

static int pl_json_serialize(char *buf, int buf_size, char paths[][PL_PATH_LEN], int count)
{
    int pos = 0;
    buf[pos++] = '[';
    for (int i = 0; i < count && pos < buf_size - 4; i++) {
        if (i > 0) buf[pos++] = ',';
        buf[pos++] = '"';
        for (const char *s = paths[i]; *s && pos < buf_size - 4; s++) {
            if (*s == '"' || *s == '\\') buf[pos++] = '\\';
            buf[pos++] = *s;
        }
        buf[pos++] = '"';
    }
    buf[pos++] = ']';
    buf[pos] = 0;
    return pos;
}

static bool plist_save(int pl_idx)
{
    if (pl_idx < 0 || pl_idx >= PL_SAVED_MAX) return false;
    char path[16];
    snprintf(path, sizeof(path), "/pl/%d.json", pl_idx + 1);
    char *buf = (char*)malloc(8192);
    if (!buf) return false;
    int len = pl_json_serialize(buf, 8192, s_plists[pl_idx].paths, s_plists[pl_idx].count);
    File f = SD.open(path, FILE_WRITE);
    if (!f) { free(buf); return false; }
    f.print(buf);
    f.close();
    free(buf);
    return true;
}

static void plists_load_all(void)
{
    if (SD.cardType() == CARD_NONE) return;
    SD.mkdir("/pl");
    for (int i = 0; i < PL_SAVED_MAX; i++) {
        s_plists[i].count = 0;
        memset(s_plists[i].paths, 0, sizeof(s_plists[i].paths));
        char path[16];
        snprintf(path, sizeof(path), "/pl/%d.json", i + 1);
        if (!SD.exists(path)) continue;
        File f = SD.open(path, "r");
        if (!f) continue;
        int fsize = f.size();
        if (fsize > 8192) { f.close(); continue; }
        char *buf = (char*)malloc(fsize + 1);
        if (!buf) { f.close(); continue; }
        int len = f.read((uint8_t*)buf, fsize);
        buf[len] = 0;
        f.close();
        s_plists[i].count = pl_json_parse(buf, len, s_plists[i].paths, PL_ENTRY_MAX);
        free(buf);
    }
    plists_refresh_source_labels();
}

static void cb_plist_load(lv_timer_t *t)
{
    plists_load_all();
    lv_timer_del(tmrPlistLoad);
    tmrPlistLoad = NULL;
}

static void browser_populate(lv_obj_t *list)
{
    lv_obj_clean(list);
    browser_n_entries = 0;
    static const lv_color_t accents[] = {
        COL_GREEN, COL_PINK, COL_TXT, COL_GRAY,
        COL_GREEN, COL_PINK,
    };

    if (SD.cardType() == CARD_NONE) {
        lv_obj_t *e = lv_list_add_text(list, "SD card not mounted");
        lv_obj_set_style_text_color(e, COL_PINK, 0);
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
        lv_obj_set_style_text_color(e, COL_PINK, 0);
        return;
    }

    if (strcmp(browser_cwd, "/") != 0 && browser_n_entries < BROWSER_MAX_ENTRIES) {
        snprintf(browser_entries[browser_n_entries], BROWSER_PATH_LEN, "..");
        browser_is_dir[browser_n_entries] = true;
        lv_obj_t *btn = lv_list_add_btn(list, LV_SYMBOL_UP, "..");
        lv_obj_set_style_bg_color(btn, COL_PANEL, 0);
        lv_obj_set_style_text_color(btn, COL_GREEN, 0);
        lv_obj_set_style_text_font(btn, &lv_font_montserrat_14, 0);
        lv_obj_set_style_border_color(btn, COL_GREEN, 0);
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
    lv_obj_set_style_text_color(title, COL_GREEN, 0);
    lv_obj_set_style_text_font(title, &UbuntuCond14, 0);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 8, 30);
    lv_label_set_text(title, "SD FILES");

    browserList = lv_list_create(screenBrowser);
    lv_obj_set_size(browserList, 304, 148);
    lv_obj_align(browserList, LV_ALIGN_TOP_LEFT, 8, 48);
    lv_obj_set_style_bg_color(browserList, COL_BG, 0);
    lv_obj_set_style_border_color(browserList, COL_GRAY, 0);
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
    lv_label_set_text(bl, LV_SYMBOL_LEFT " PLAYER");
    lv_obj_center(bl);
    lv_obj_add_event_cb(back, on_back_to_player, LV_EVENT_CLICKED, NULL);

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
    lv_obj_set_style_text_color(title, COL_GREEN, 0);
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
    lv_obj_set_style_text_color(lblLdrLive, COL_GREEN, 0);
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
            lv_label_set_text_fmt(lblFolder, "%s", last);
        } else {
            lv_label_set_text(lblFolder, "");
        }
    }
    // Reset progress bar and time
    if (barProgress) lv_bar_set_value(barProgress, 0, LV_ANIM_OFF);
    if (lblTimeElapsed) lv_label_set_text(lblTimeElapsed, "0:00");
    if (lblTimeDuration) lv_label_set_text(lblTimeDuration, "0:00");
    update_play_pause_btn();
    plists_refresh_player_icons();
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
    // FPS counting
    s_frame_cnt++;
    uint32_t now = lv_tick_get();
    if (now - s_fps_tick >= 1000) {
        s_fps_val = s_frame_cnt * 1000 / (now - s_fps_tick);
        s_frame_cnt = 0;
        s_fps_tick = now;
    }

    if (lv_scr_act() != screenPlayer) return;
    uint32_t vu = audioGetRMS();
    // RMS values are post-volume (16-bit per channel).
    // Compensate for volume so VU shows signal level regardless of volume setting.
    // Volume slider 0-100 maps to audio steps 0-21; gain = (step*3120)^2 >> 16.
    // We normalize by multiplying RMS by (65535/max(gain,1)) so the VU
    // always reflects the original signal, not the attenuated one.
    uint8_t volPct = audioGetVolumePerCent();
    // Approximate gain factor: at volPct%, step = volPct*21/100
    // gain = (step * 3120)^2 >> 16.  At 100%: gain ≈ 65535.  At 50%: gain ≈ 16384.
    // We boost raw RMS by 65535/max(gain, 100) to normalize.
    uint32_t step = (uint32_t)(volPct * 21) / 100;
    if (step < 1) step = 1;
    uint32_t g = (uint32_t)step * 3120;
    uint32_t gain = ((g * g) + 65535) >> 16;
    if (gain < 100) gain = 100;
    uint32_t boost = 65535 / gain;  // e.g. 65535/65535=1 at 100%, ~4 at 25%

    uint16_t r16 = ((vu >> 16) & 0xFFFF);
    uint16_t l16 = (vu & 0xFFFF);
    // Apply volume-compensated boost, then log-scale
    auto vu_map = [boost](uint16_t raw) -> int {
        uint32_t compensated = (uint32_t)raw * boost;
        if (compensated > 65535) compensated = 65535;
        if (compensated == 0) return 0;
        // Log curve: maps 1..65535 → ~6..100
        int v = (int)(100.0f * log2f((float)compensated + 3.0f) / log2f(2048.0f));
        return v > 100 ? 100 : v;
    };
    lv_bar_set_value(vuL, vu_map(l16), LV_ANIM_OFF);
    lv_bar_set_value(vuR, vu_map(r16), LV_ANIM_OFF);

    // Update progress bar and time labels while playing
    if (s_playing) {
        uint32_t cur = audioGetCurrentTime();
        uint32_t dur = audioGetFileDuration();
        if (dur > 0) {
            lv_bar_set_value(barProgress, (int)((cur * 100) / dur), LV_ANIM_OFF);
        } else {
            lv_bar_set_value(barProgress, 0, LV_ANIM_OFF);
        }
        char tb[8];
        fmt_time(cur, tb, sizeof(tb));
        lv_label_set_text(lblTimeElapsed, tb);
        fmt_time(dur, tb, sizeof(tb));
        lv_label_set_text(lblTimeDuration, tb);
    }
}

static void fmt_time(uint32_t secs, char *buf, size_t n)
{
    snprintf(buf, n, "%u:%02u", (unsigned)(secs / 60), (unsigned)(secs % 60));
}

static void cb_status(lv_timer_t *t)
{
    refresh_status_bar();

    // Handle track-end advance (deferred from audio core)
    if (s_track_ended) {
        s_track_ended = false;
        advance_track();
    }

    bool now = audioIsPlaying();
    if (now != s_playing) {
        s_playing = now;
        update_play_pause_btn();
        if (lblNowPlaying) {
            lv_label_set_text(lblNowPlaying,
                now ? LV_SYMBOL_PAUSE " NOW PLAYING" : LV_SYMBOL_PLAY " STOPPED");
            lv_obj_set_style_text_color(lblNowPlaying, now ? COL_GREEN : COL_TXT_DIM, 0);
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
        case 1: // PREV
            if (s_pl_count > 0 && s_pl_idx >= 0) {
                if (s_play_mode == PM_SHUFFLE) {
                    int prev;
                    do { prev = esp_random() % s_pl_count; } while (prev == s_pl_idx && s_pl_count > 1);
                    playlist_play_idx(prev);
                } else {
                    int prev = s_pl_idx - 1;
                    if (prev < 0) prev = s_pl_count - 1;
                    playlist_play_idx(prev);
                }
            }
            break;
        case 2: // PLAY / PAUSE toggle
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
        case 3: // NEXT
            if (s_pl_count > 0 && s_pl_idx >= 0) {
                if (s_play_mode == PM_SHUFFLE) {
                    int next;
                    do { next = esp_random() % s_pl_count; } while (next == s_pl_idx && s_pl_count > 1);
                    playlist_play_idx(next);
                } else {
                    int next = s_pl_idx + 1;
                    if (next >= s_pl_count) next = 0;
                    playlist_play_idx(next);
                }
            }
            break;
        case 4: // FOLDER - jump to browser
            if (s_pl_dir[0]) {
                snprintf(browser_cwd, sizeof(browser_cwd), "%s", s_pl_dir);
            } else {
                snprintf(browser_cwd, sizeof(browser_cwd), "/music");
            }
            browser_populate(browserList);
            lv_scr_load_anim(screenBrowser, LV_SCR_LOAD_ANIM_MOVE_LEFT, 200, 0, false);
            refresh_status_bar();
            break;
        case 5: // VOL TOGGLE
            s_vol_visible = !s_vol_visible;
            if (s_vol_visible) {
                if (volTag)    lv_obj_clear_flag(volTag, LV_OBJ_FLAG_HIDDEN);
                if (volSlider) lv_obj_clear_flag(volSlider, LV_OBJ_FLAG_HIDDEN);
                if (lblVol)    lv_obj_clear_flag(lblVol, LV_OBJ_FLAG_HIDDEN);
            } else {
                if (volTag)    lv_obj_add_flag(volTag, LV_OBJ_FLAG_HIDDEN);
                if (volSlider) lv_obj_add_flag(volSlider, LV_OBJ_FLAG_HIDDEN);
                if (lblVol)    lv_obj_add_flag(lblVol, LV_OBJ_FLAG_HIDDEN);
            }
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

static void plist_play(int pl_idx)
{
    if (pl_idx < 0 || pl_idx >= PL_SAVED_MAX) return;
    saved_playlist_t *pl = &s_plists[pl_idx];
    if (pl->count == 0) return;

    s_pl_count = 0;
    s_pl_idx = 0;
    for (int i = 0; i < pl->count && i < PLAYLIST_MAX; i++) {
        const char *full = pl->paths[i];
        const char *base = strrchr(full, '/');
        if (base) base++; else base = full;
        strncpy(s_pl_files[i], base, PATH_LEN - 1);
        s_pl_files[i][PATH_LEN - 1] = 0;
        strncpy(s_pl_fullpaths[i], full, PL_PATH_LEN - 1);
        s_pl_fullpaths[i][PL_PATH_LEN - 1] = 0;
        if (i == 0) {
            char dir[PL_PATH_LEN];
            strncpy(dir, full, sizeof(dir) - 1);
            dir[sizeof(dir) - 1] = 0;
            char *sl = strrchr(dir, '/');
            if (sl) *sl = 0; else strcpy(dir, "/");
            strncpy(s_pl_dir, dir, PATH_LEN - 1);
            s_pl_dir[PATH_LEN - 1] = 0;
        }
        s_pl_count++;
    }
    s_pl_from_saved = true;
    playlist_play_idx(0);
    lv_scr_load_anim(screenPlayer, LV_SCR_LOAD_ANIM_MOVE_RIGHT, 200, 0, false);
    refresh_status_bar();
}

static void on_plist_source_pick(lv_event_t *e)
{
    int pl_idx = (int)(intptr_t)lv_event_get_user_data(e);
    plist_play(pl_idx);
}

static void on_plist_add_track(lv_event_t *e)
{
    int pl_idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (pl_idx < 0 || pl_idx >= PL_SAVED_MAX) return;
    if (!s_track[0]) return;

    saved_playlist_t *pl = &s_plists[pl_idx];
    // toggle: remove if already in list, add if not
    for (int i = 0; i < pl->count; i++) {
        if (strcmp(pl->paths[i], s_track) == 0) {
            // remove
            for (int j = i; j < pl->count - 1; j++)
                strncpy(pl->paths[j], pl->paths[j+1], PL_PATH_LEN);
            pl->paths[pl->count - 1][0] = 0;
            pl->count--;
            plist_save(pl_idx);
            plists_refresh_player_icons();
            plists_refresh_source_labels();
            return;
        }
    }
    // add
    if (pl->count >= PL_ENTRY_MAX) return;
    strncpy(pl->paths[pl->count], s_track, PL_PATH_LEN - 1);
    pl->paths[pl->count][PL_PATH_LEN - 1] = 0;
    pl->count++;
    plist_save(pl_idx);
    plists_refresh_player_icons();
    plists_refresh_source_labels();
}

static void plists_refresh_player_icons(void)
{
    for (int i = 0; i < PL_SAVED_MAX; i++) {
        if (!btnPlIcon[i]) continue;
        bool in_list = false;
        if (s_track[0]) {
            for (int j = 0; j < s_plists[i].count; j++) {
                if (strcmp(s_plists[i].paths[j], s_track) == 0) {
                    in_list = true;
                    break;
                }
            }
        }
        // child 0 = label, child 1 = underline line
        lv_obj_t *line = lv_obj_get_child(btnPlIcon[i], 1);
        if (in_list) {
            lv_obj_clear_flag(line, LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_style_bg_opa(line, LV_OPA_COVER, 0);
        } else {
            lv_obj_add_flag(line, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

static void plists_refresh_source_labels(void)
{
    for (int i = 0; i < PL_SAVED_MAX; i++) {
        if (!lblPlCount[i]) continue;
        lv_label_set_text_fmt(lblPlCount[i], "%s (%d)", pl_names[i], s_plists[i].count);
    }
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

static void on_play_mode(lv_event_t *e)
{
    s_play_mode = (play_mode_t)((s_play_mode + 1) % 3);
    if (!lblPlayMode) return;
    switch (s_play_mode) {
        case PM_LOOP_ALL:
            lv_label_set_text(lblPlayMode, LV_SYMBOL_LOOP);
            lv_obj_set_style_border_color(btnPlayMode, COL_GRAY, 0);
            lv_obj_set_style_text_color(btnPlayMode, COL_GRAY, 0);
            lv_obj_set_style_bg_color(btnPlayMode, COL_GRAY, LV_STATE_PRESSED);
            break;
        case PM_SHUFFLE:
            lv_label_set_text(lblPlayMode, LV_SYMBOL_SHUFFLE);
            lv_obj_set_style_border_color(btnPlayMode, COL_TXT, 0);
            lv_obj_set_style_text_color(btnPlayMode, COL_TXT, 0);
            lv_obj_set_style_bg_color(btnPlayMode, COL_TXT, LV_STATE_PRESSED);
            break;
        case PM_REPEAT_ONE:
            lv_label_set_text(lblPlayMode, LV_SYMBOL_REFRESH);
            lv_obj_set_style_border_color(btnPlayMode, COL_PINK, 0);
            lv_obj_set_style_text_color(btnPlayMode, COL_PINK, 0);
            lv_obj_set_style_bg_color(btnPlayMode, COL_PINK, LV_STATE_PRESSED);
            break;
    }
}

static void on_source_tap(lv_event_t *e)
{
    lv_scr_load_anim(screenSources, LV_SCR_LOAD_ANIM_MOVE_LEFT, 200, 0, false);
    refresh_status_bar();
}

static void advance_track(void)
{
    if (s_pl_count == 0 || s_pl_idx < 0) return;
    switch (s_play_mode) {
        case PM_LOOP_ALL:
            if (s_pl_idx < s_pl_count - 1)
                playlist_play_idx(s_pl_idx + 1);
            else
                playlist_play_idx(0);
            break;
        case PM_SHUFFLE:
            if (s_pl_count == 1) {
                playlist_play_idx(0);
            } else {
                int next;
                do { next = esp_random() % s_pl_count; } while (next == s_pl_idx);
                playlist_play_idx(next);
            }
            break;
        case PM_REPEAT_ONE:
            playlist_play_idx(s_pl_idx);
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
    s_track_ended = true;
}