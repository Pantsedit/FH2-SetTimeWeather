// Offline tests for Time Lock, run without the game:
//
//   offline_test.exe GAME.exe A_VideoOptions.xaml OUT_DIR [more.xaml ...]
//
// 1. Maps the real game EXE (sections at their RVAs, executable) and checks
//    every guard in clock_hold.c and settings_page.c passes, and that a
//    changed byte makes them refuse.
// 2. Installs the real clock hook into the mapped image and drives the game's
//    own TimeOfDay::Update / SetTime / GetTime code on a fake clock object
//    through the free-roam, race, hour-change and lock-off cases.
// 3. Runs the Settings page edit on each XAML file (written to OUT_DIR as
//    <name>.timelock.xaml) and checks it is applied once and only to the Video page.
// 4. Hour parsing (numbers, HH:MM, presets).

#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include "../src/clock_hold.h"
#include "../src/config.h"
#include "../src/entry_hook.h"
#include "../src/settings_page.h"
#include "../src/weather_hold.h"

static int failures;

static void log_line(const char *text) { printf("    log: %s", text); }

#define CHECK(cond, ...)                         \
    do {                                         \
        if (cond) printf("  ok   ");             \
        else { printf("  FAIL "); ++failures; }  \
        printf(__VA_ARGS__);                     \
        printf("\n");                            \
    } while (0)

static unsigned char *read_file(const char *path, size_t *size) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    unsigned char *data = malloc((size_t)n + 1);
    if (data && fread(data, 1, (size_t)n, f) != (size_t)n) { free(data); data = NULL; }
    fclose(f);
    if (data) { data[n] = 0; *size = (size_t)n; }
    return data;
}

static unsigned char *map_image(const unsigned char *file, size_t *image_size) {
    uint32_t pe;
    memcpy(&pe, file + 0x3c, 4);
    uint16_t count, opt_size;
    memcpy(&count, file + pe + 6, 2);
    memcpy(&opt_size, file + pe + 20, 2);
    uint32_t size_of_image, size_of_headers;
    memcpy(&size_of_image, file + pe + 24 + 56, 4);
    memcpy(&size_of_headers, file + pe + 24 + 60, 4);
    unsigned char *image = VirtualAlloc(NULL, size_of_image, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE);
    if (!image) return NULL;
    memcpy(image, file, size_of_headers);
    const unsigned char *section = file + pe + 24 + opt_size;
    for (unsigned i = 0; i < count; ++i, section += 40) {
        uint32_t vsize, va, rsize, raw;
        memcpy(&vsize, section + 8, 4);
        memcpy(&va, section + 12, 4);
        memcpy(&rsize, section + 16, 4);
        memcpy(&raw, section + 20, 4);
        uint32_t n = rsize < vsize || !vsize ? rsize : vsize;
        if (va + n <= size_of_image) memcpy(image + va, file + raw, n);
    }
    *image_size = size_of_image;
    return image;
}

// ---------------------------------------------------------------- guards

static void test_guards(unsigned char *image, size_t size) {
    printf("guards against the real EXE\n");
    CHECK(clock_hold_verify(image, size, log_line), "clock hook: all expected bytes match");
    CHECK(settings_page_verify(image, size, log_line), "settings page: all expected bytes match");
    static const uint32_t probes[] = {0x197b170, 0x197b1ae, 0x197aec6, 0x197a880, 0x93ca22};
    for (unsigned i = 0; i < sizeof probes / sizeof *probes; ++i) {
        image[probes[i]] ^= 0xff;
        CHECK(!clock_hold_verify(image, size, NULL), "clock hook refuses when game+0x%x is changed", probes[i]);
        image[probes[i]] ^= 0xff;
    }
    static const uint32_t page_probes[] = {0x155ee30, 0xeca80a, 0xeca810, 0xed825e, 0xed6efe, 0x4702d0, 0xeca710, 0xed81f0};
    for (unsigned i = 0; i < sizeof page_probes / sizeof *page_probes; ++i) {
        image[page_probes[i]] ^= 0xff;
        CHECK(!settings_page_verify(image, size, NULL), "settings page refuses when game+0x%x is changed", page_probes[i]);
        image[page_probes[i]] ^= 0xff;
    }
}

// ---------------------------------------------------------------- clock

typedef unsigned char (*UpdateFn)(unsigned char *tod, float dt, void *out, void *unused);
typedef void (*SetTimeFn)(unsigned char *tod, float seconds, int remap);

static float tod_time(unsigned char *tod) {
    float t;
    memcpy(&t, tod + 0x10, 4);
    return t;
}

static void test_clock(unsigned char *image, size_t size, const char *out_dir) {
    printf("clock hook driving the game's own TimeOfDay code\n");
    wchar_t ini[MAX_PATH];
    swprintf(ini, MAX_PATH, L"%hs\\test_TimeLock.ini", out_dir);
    DeleteFileW(ini);
    WritePrivateProfileStringW(L"TimeLock", L"Enabled", L"1", ini);
    WritePrivateProfileStringW(L"TimeLock", L"Hour", L"Sunset", ini);
    config_load(ini, log_line);
    CHECK(config_hour() > 17.83f && config_hour() < 17.84f, "Hour=Sunset loads as 17:50 (%.4f)", config_hour());

    CHECK(clock_hold_install(image, size, log_line), "hook installed into the mapped image");
    UpdateFn update = (UpdateFn)(image + 0x197b170);
    SetTimeFn set_time = (SetTimeFn)(image + 0x197aec0);
    static unsigned char out[0x400];
    unsigned char *tod = VirtualAlloc(NULL, 0x1000, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    float speed = 48.0f, start = 14.0f * 3600.0f;
    memcpy(tod + 0x18, &speed, 4);
    memcpy(tod + 0x14, &(float){60.0f}, 4);  // Update reads it after the advance only when +0x29 is set
    memcpy(tod + 0x10, &start, 4);

    // Free roam: running clock is set to the hour and stopped.
    update(tod, 1.0f, out, NULL);
    float held = tod_time(tod);
    CHECK(tod[0x24] == 1 && held > 64199.0f && held < 64201.0f,
          "running clock at 14:00 -> held at %.0f s (17:50 = 64200), stopped=%d", held, tod[0x24]);
    for (int i = 0; i < 100; ++i) update(tod, 1.0f, out, NULL);
    CHECK(tod_time(tod) == held, "100 more frames: still %.0f s", tod_time(tod));

    // Hour changed in Settings: our clock moves.
    config_set_hour(6.5f);
    update(tod, 1.0f, out, NULL);
    CHECK(tod_time(tod) == 6.5f * 3600.0f && tod[0x24] == 1, "hour 6.5 from Settings -> %.0f s (23400), stopped", tod_time(tod));
    wchar_t saved[32];
    GetPrivateProfileStringW(L"TimeLock", L"Hour", L"", saved, 32, ini);
    CHECK(!wcscmp(saved, L"6.50"), "hour saved to the ini as %ls", saved);

    // A race sets its own time and stops the clock: we leave it, even if the hour changes.
    set_time(tod, 10.0f * 3600.0f, 1);
    tod[0x24] = 1;
    config_set_hour(20.0f);
    update(tod, 1.0f, out, NULL);
    CHECK(tod_time(tod) == 36000.0f, "race time 10:00 set by the game is kept (%.0f s)", tod_time(tod));

    // Race with a timed transition: running towards a target; we leave it alone.
    tod[0x24] = 0;
    tod[0x28] = 1;
    float target = 11.0f * 3600.0f;
    memcpy(tod + 0x1c, &target, 4);
    update(tod, 1.0f, out, NULL);
    CHECK(tod_time(tod) == 36048.0f, "targeted transition keeps running (%.0f s = 10:00 + 48)", tod_time(tod));
    tod[0x28] = 0;

    // Free roam resumes (start_time): back to the locked hour.
    update(tod, 1.0f, out, NULL);
    CHECK(tod_time(tod) == 72000.0f && tod[0x24] == 1, "clock restarted -> held at %.0f s (20:00)", tod_time(tod));

    // Game's own stop lock: untouched.
    tod[0x24] = 0;
    tod[0x27] = 1;
    float before = tod_time(tod);
    update(tod, 1.0f, out, NULL);
    CHECK(tod_time(tod) == before + 48.0f && tod[0x24] == 0, "stop-locked clock runs untouched (%.0f s)", tod_time(tod));
    tod[0x27] = 0;
    update(tod, 1.0f, out, NULL);  // held again

    // Lock off from Settings: our stopped clock runs again.
    config_set_enabled(0);
    update(tod, 1.0f, out, NULL);
    CHECK(tod[0x24] == 0 && tod_time(tod) == 72048.0f, "lock off -> clock running again (%.0f s)", tod_time(tod));
    update(tod, 1.0f, out, NULL);
    CHECK(tod_time(tod) == 72096.0f, "and keeps advancing (%.0f s)", tod_time(tod));
    GetPrivateProfileStringW(L"TimeLock", L"Enabled", L"", saved, 32, ini);
    CHECK(!wcscmp(saved, L"0"), "Enabled=0 saved to the ini");

    // Lock off must not restart a clock the game stopped.
    config_set_enabled(1);
    update(tod, 1.0f, out, NULL);  // held
    set_time(tod, 3600.0f, 1);     // cutscene sets 01:00 and keeps it stopped
    config_set_enabled(0);
    update(tod, 1.0f, out, NULL);
    CHECK(tod[0x24] == 1 && tod_time(tod) == 3600.0f, "lock off leaves a clock the game stopped itself");

    // Midnight wrap is the game's: 23:59:59 running with the lock off wraps.
    float late = 86399.0f;
    memcpy(tod + 0x10, &late, 4);
    tod[0x24] = 0;
    update(tod, 1.0f, out, NULL);
    CHECK(tod_time(tod) == 47.0f, "game wraps past midnight as usual (%.0f s)", tod_time(tod));
}

// ---------------------------------------------------------------- XAML

static void test_xaml(const char *path, const char *out_dir) {
    size_t length = 0;
    char *text = (char *)read_file(path, &length);
    printf("XAML edit: %s\n", path);
    if (!text) { CHECK(0, "cannot read %s", path); return; }
    size_t capacity = length + 65536;
    char *out = malloc(capacity), *again = malloc(capacity + 65536);
    size_t n = settings_page_edit(text, length, out, capacity);
    int hud = settings_page_is_xaml(text, length);
    if (!hud) {
        CHECK(n == 0, "not the Video page: left unchanged");
    } else {
        CHECK(n > length, "rows inserted (%zu -> %zu bytes)", length, n);
        CHECK(n && strstr(out, "{Binding TimeLockEnabled}") && strstr(out, "{Binding TimeLockHour, Mode=TwoWay}") &&
                  strstr(out, "{Binding TimeLockWeather}") && strstr(out, "{Binding TimeLockWeatherOptions}"),
              "all bindings present (lock, hour, weather and its list)");
        CHECK(n && settings_page_edit(out, n, again, capacity + 65536) == 0, "second pass leaves it alone");
        // Original text survives in order: removing our two inserts gives the input back.
        const char *r1 = strstr(out, "    <local:TileMenuDataItem x:Key=\"TimeLock_Item_Enabled\"");
        const char *r1e = r1 ? strstr(r1, "</Style>\r\n        ") : NULL;
        const char *r2 = strstr(out, "    <local:SpinnerControl Label=\"Time of Day Lock\"");
        static const char row_end[] = "ItemsSource=\"{Binding TimeLockWeatherOptions}\" Style=\"{StaticResource A_Style_SpinnerControl_Default}\"/>\r\n                                ";
        const char *r2e = r2 ? strstr(r2, row_end) : NULL;
        if (r1 && r1e && r2 && r2e) {
            r1e += strlen("</Style>\r\n        ");
            r2e += strlen(row_end);
            size_t k = 0;
            char *rebuilt = malloc(length + 1);
            memcpy(rebuilt + k, out, (size_t)(r1 - out)), k += (size_t)(r1 - out);
            memcpy(rebuilt + k, r1e, (size_t)(r2 - r1e)), k += (size_t)(r2 - r1e);
            memcpy(rebuilt + k, r2e, n - (size_t)(r2e - out)), k += n - (size_t)(r2e - out);
            // The only other change allowed: the stock Height="110" of the OptionsList tag becomes 275 (five rows).
            char *expected = malloc(length + 1);
            memcpy(expected, text, length + 1);
            const char *list = strstr(text, "x:Name=\"OptionsList\"");
            const char *tag = list, *tag_end = list ? strchr(list, '>') : NULL;
            while (tag && tag > text && *tag != '<') --tag;
            int grew = 0;
            if (tag && tag_end) {
                char *h = strstr(expected + (tag - text), "Height=\"110\"");
                if (h && h < expected + (tag_end - text)) { memcpy(h, "Height=\"275\"", 12); grew = 1; }
            }
            CHECK(k == length && !memcmp(rebuilt, expected, length),
                  "everything else is byte-for-byte unchanged (list height %s)", grew ? "110 -> 275" : "left as is");
            free(rebuilt), free(expected);
        } else {
            CHECK(0, "could not locate the inserts");
        }
        const char *name = strrchr(path, '\\');
        name = name ? name + 1 : path;
        char out_path[MAX_PATH];
        snprintf(out_path, sizeof out_path, "%s\\%.*s.timelock.xaml", out_dir, (int)(strcspn(name, ".")), name);
        FILE *f = fopen(out_path, "wb");
        if (f) { fwrite(out, 1, n, f); fclose(f); printf("    wrote %s\n", out_path); }
    }
    free(text), free(out), free(again);
}

// ---------------------------------------------------------------- weather

static unsigned char fake_weather[0x400];
static unsigned char *fake_tod_for_weather;
static int set_calls, release_calls;
static char last_preset[64];

static unsigned char *test_weather_of(unsigned char *tod) { return tod == fake_tod_for_weather ? fake_weather : NULL; }

// What the game's SetControl does to W+0xe0: copy the block, then turn a
// preset's severity into atmospheric wetness (the has-severity flag is cleared).
static void test_set_control(unsigned char *weather, const unsigned char *control) {
    ++set_calls;
    memcpy(weather + 0xe0, control, 0x3c);
    if (weather[0xe0]) {
        weather[0xe8] = 0;
        weather[0xf0] = 1;
        memcpy(weather + 0xf4, &(float){0.5f}, 4);
    }
}

static void test_release(unsigned char *weather) {
    ++release_calls;
    unsigned char clear[0x40];
    weather_hold_build_control(WEATHER_DYNAMIC, 0, clear);
    clear[0x32] = 1, clear[0x38] = 1;
    memcpy(clear + 0x34, &(float){1.0f}, 4);
    memcpy(weather + 0xe0, clear, 0x3c);
}

typedef struct { union { char buffer[16]; const char *pointer; }; size_t size, capacity; } TestString;

static int test_find_preset(unsigned char *weather, const void *name) {
    (void)weather;
    const TestString *s = name;
    const char *text = s->capacity >= 16 ? s->pointer : s->buffer;
    snprintf(last_preset, sizeof last_preset, "%.*s", (int)s->size, text);
    return (int)s->size;  // any distinct index
}

static void game_sets_weather(void) {  // e.g. a race with its own weather: flags set by the game
    memset(fake_weather + 0xe0, 0, 0x3c);
    fake_weather[0xe0] = 1;
    fake_weather[0xe4] = 7;
    fake_weather[0x120] ^= 1;
}

static void test_weather(unsigned char *image, size_t size, const char *out_dir) {
    printf("weather hold\n");
    CHECK(weather_hold_verify(image, size, log_line), "weather: all expected bytes match");
    static const uint32_t probes[] = {0x6ae293, 0x21e0c3, 0x2e2e93, 0x1981869, 0x1981cd0, 0x1981e51, 0x92f786, 0x8e9a9d, 0x30071a};
    for (unsigned i = 0; i < sizeof probes / sizeof *probes; ++i) {
        image[probes[i]] ^= 0xff;
        CHECK(!weather_hold_verify(image, size, NULL), "weather refuses when game+0x%x is changed", probes[i]);
        image[probes[i]] ^= 0xff;
    }

    // The control blocks match the game's own online-event weather options.
    unsigned char c[0x40];
    float env, rain;
    weather_hold_build_control(WEATHER_RAIN, 9, c);
    memcpy(&env, c + 0x1c, 4), memcpy(&rain, c + 0x24, 4);
    CHECK(c[0] == 1 && *(int *)(c + 4) == 9 && c[0x18] == 1 && env == 1.0f && c[0x20] == 1 && rain == 0.8f && !c[0x08] && !c[0x10] &&
              c[0x32] == 1 && c[0x38] == 1 && !strcmp(weather_hold_preset_name(WEATHER_RAIN), "ThreateningStormACloudyB"),
          "Rain = preset ThreateningStormACloudyB, road wetness 1.0, rain 0.8");
    weather_hold_build_control(WEATHER_SUNNY, 0, c);
    memcpy(&env, c + 0x1c, 4), memcpy(&rain, c + 0x24, 4);
    CHECK(!strcmp(weather_hold_preset_name(WEATHER_SUNNY), "Clear") && env == 0 && rain == 0, "Sunny = Clear, dry");
    CHECK(!strcmp(weather_hold_preset_name(WEATHER_WET), "ThreateningStormCCloudyA") &&
              !strcmp(weather_hold_preset_name(WEATHER_STORM), "StormBCloudyA") && !weather_hold_preset_name(WEATHER_DYNAMIC),
          "Wet / Storm presets, Dynamic has none");

    wchar_t ini[MAX_PATH];
    swprintf(ini, MAX_PATH, L"%hs\\test_TimeLock.ini", out_dir);
    DeleteFileW(ini);
    config_load(ini, log_line);
    config_load_weather();
    CHECK(config_weather() == WEATHER_DYNAMIC, "no [Weather] Mode: Dynamic by default");

    unsigned char tod[0x40];
    fake_tod_for_weather = tod;
    memset(fake_weather, 0, sizeof fake_weather);
    test_release(fake_weather);  // free roam: released
    release_calls = 0;
    weather_hold_test_setup(test_set_control, test_release, test_find_preset, test_weather_of, log_line);

    for (int i = 0; i < 5; ++i) weather_hold_frame(tod);
    CHECK(set_calls == 0 && release_calls == 0, "Dynamic: the game's weather is never touched");

    config_set_weather(WEATHER_RAIN);
    weather_hold_frame(tod);
    CHECK(set_calls == 1 && !strcmp(last_preset, "ThreateningStormACloudyB") && fake_weather[0xe0] == 1,
          "Rain chosen in free roam -> one SetControl with preset %s (long name passed by pointer)", last_preset);
    for (int i = 0; i < 100; ++i) weather_hold_frame(tod);
    CHECK(set_calls == 1, "100 more frames: no further calls");
    wchar_t saved[32];
    GetPrivateProfileStringW(L"Weather", L"Mode", L"", saved, 32, ini);
    CHECK(!wcscmp(saved, L"Rain"), "[Weather] Mode=%ls saved", saved);

    config_set_weather(WEATHER_SUNNY);
    weather_hold_frame(tod);
    CHECK(set_calls == 2 && !strcmp(last_preset, "Clear"), "changed to Sunny -> preset %s (short name inline)", last_preset);

    game_sets_weather();  // race / car meet sets its own weather
    config_set_weather(WEATHER_STORM);
    for (int i = 0; i < 10; ++i) weather_hold_frame(tod);
    CHECK(set_calls == 2, "weather the game set itself is left alone, even when the setting changes");

    test_release(fake_weather);  // race over: the game releases to dynamic
    weather_hold_frame(tod);
    CHECK(set_calls == 3 && !strcmp(last_preset, "StormBCloudyA"), "game released it -> held at Storm again");

    config_set_weather(WEATHER_DYNAMIC);
    release_calls = 0;
    weather_hold_frame(tod);
    CHECK(release_calls == 1 && !fake_weather[0xe0], "Dynamic chosen -> the game's own Release");
    for (int i = 0; i < 10; ++i) weather_hold_frame(tod);
    CHECK(set_calls == 3 && release_calls == 1, "and nothing more");

    config_set_weather(WEATHER_WET);
    weather_hold_frame(tod);
    game_sets_weather();
    config_set_weather(WEATHER_DYNAMIC);
    release_calls = 0;
    weather_hold_frame(tod);
    CHECK(release_calls == 0, "Dynamic doesn't release weather the game set itself");

    unsigned char other_tod[0x40];
    int before = set_calls;
    config_set_weather(WEATHER_RAIN);
    weather_hold_frame(other_tod);
    CHECK(set_calls == before, "a clock from another world finds no weather manager: nothing done");

    int mode = -1;
    CHECK(config_parse_weather(L"storm", &mode) && mode == WEATHER_STORM && config_parse_weather(L"Clear", &mode) &&
              mode == WEATHER_SUNNY && config_parse_weather(L"Current", &mode) && mode == WEATHER_DYNAMIC &&
              !config_parse_weather(L"Cloudy", &mode) && !config_parse_weather(L"", &mode),
          "Mode names: Dynamic/Current, Sunny/Clear, Wet, Rain, Storm; others refused");
    DeleteFileW(ini);
}

static void test_hours(void) {
    printf("hour parsing\n");
    static const struct { const wchar_t *text; int ok; float hours; } cases[] = {
        {L"18.5", 1, 18.5f}, {L"18:30", 1, 18.5f}, {L" 6 ", 1, 6.0f}, {L"0", 1, 0.0f}, {L"23.99", 1, 23.99f},
        {L"Dawn", 1, 5.75f}, {L"noon", 1, 12.0f}, {L"NIGHT", 1, 23.0f}, {L"12 ; comment", 1, 12.0f},
        {L"24", 0, 0}, {L"-1", 0, 0}, {L"dusk", 0, 0}, {L"", 0, 0}, {L"18:75", 0, 0}, {L"7pm", 0, 0},
    };
    for (unsigned i = 0; i < sizeof cases / sizeof *cases; ++i) {
        float h = -1;
        int ok = config_parse_hour(cases[i].text, L"Z:\\no\\such\\file.ini", &h);
        CHECK(ok == cases[i].ok && (!ok || (h > cases[i].hours - 0.001f && h < cases[i].hours + 0.001f)),
              "\"%ls\" -> %s %.3f", cases[i].text, ok ? "valid" : "invalid", h);
    }
}

int main(int argc, char **argv) {
    if (argc < 4) {
        printf("usage: offline_test GAME.exe A_VideoOptions.xaml OUT_DIR [more.xaml ...]\n");
        return 2;
    }
    size_t file_size = 0, image_size = 0;
    unsigned char *file = read_file(argv[1], &file_size);
    unsigned char *image = file ? map_image(file, &image_size) : NULL;
    if (!image) { printf("cannot map %s\n", argv[1]); return 2; }
    test_hours();
    test_guards(image, image_size);
    test_clock(image, image_size, argv[3]);
    test_weather(image, image_size, argv[3]);
    test_xaml(argv[2], argv[3]);
    for (int i = 4; i < argc; ++i) test_xaml(argv[i], argv[3]);
    printf(failures ? "\n%d FAILED\n" : "\nall passed\n", failures);
    return failures != 0;
}
