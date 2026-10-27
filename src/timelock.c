// Time Lock: an Extra Options plugin that holds Forza Horizon 2's free-roam
// time of day at a chosen hour.
//
//   clock_hold.c     the clock: a guarded hook on TimeOfDay::Update that uses
//                    the game's own SetTime and "stopped" flag, like the
//                    stateflow's set_time_of_day + stop_time.
//   settings_page.c  "Time of Day Lock" and "Locked Hour" rows on Settings > Video.
//   weather_hold.c   the weather: the game's WeatherManager::SetControl with
//                    the game's own Sunny/Wet/Rain/Storm options, applied
//                    only while the game has left free-roam weather dynamic.
//   config.c         TimeLock.ini (read at start, written by the Settings rows).
//
// Everything is in memory; no game file is replaced. Each patch first checks
// the exact bytes it expects and does nothing (with a log line) otherwise.

#include "fh2_plugin.h"
#include <stdio.h>
#include <wchar.h>

#include "clock_hold.h"
#include "config.h"
#include "settings_page.h"
#include "weather_hold.h"

static const FH2PluginContext *context;

static void log_line(const char *text) {
    if (context && context->log_utf8) context->log_utf8(text);
}

__declspec(dllexport) BOOL WINAPI FH2PluginInitialize(const FH2PluginContext *ctx) {
    if (!ctx || ctx->size < sizeof(FH2PluginContext) || ctx->api_version < 1 || !ctx->game_image) return FALSE;
    context = ctx;
    unsigned char *image = ctx->game_image;
    size_t image_size = ctx->game_image_size;

    // TimeLock.ini sits in the mod folder, beside plugins\.
    wchar_t ini[MAX_PATH], dump[MAX_PATH];
    swprintf(ini, MAX_PATH, L"%ls\\..\\TimeLock.ini", ctx->plugins_directory);
    config_load(ini, log_line);
    config_load_weather();

    int clock = clock_hold_install(image, image_size, log_line);
    int weather = clock && weather_hold_init(image, image_size, log_line);  // runs from the clock hook
    int page = 0;
    if (clock && GetPrivateProfileIntW(L"Settings", L"ShowInGameSettings", 1, ini)) {  // rows are useless without the clock
        swprintf(dump, MAX_PATH, L"%ls\\..\\seen_A_VideoOptions.xaml", ctx->plugins_directory);
        page = settings_page_install(image, image_size, log_line,
                                     GetPrivateProfileIntW(L"Settings", L"SaveSettingsPageCopy", 0, ini) ? dump : NULL, weather);
    }
    char line[200];
    snprintf(line, sizeof line, "TIME LOCK: ready (clock hold %s, weather hold %s, Settings rows %s).\r\n",
             clock ? "on" : "OFF", weather ? "on" : "OFF", page ? "on" : "off");
    log_line(line);
    return TRUE;
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved) {
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) DisableThreadLibraryCalls(instance);
    return TRUE;
}
