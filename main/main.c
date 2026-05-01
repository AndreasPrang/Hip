#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/i2c.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define OLED_WIDTH 128
#define OLED_HEIGHT 64
#define OLED_PAGES (OLED_HEIGHT / 8)
#define I2C_PORT I2C_NUM_0
#define I2C_FREQ_HZ 400000

#define SDA_GPIO CONFIG_HIP_I2C_SDA_GPIO
#define SCL_GPIO CONFIG_HIP_I2C_SCL_GPIO
#define OLED_ADDR CONFIG_HIP_OLED_I2C_ADDRESS
#define OLED_COL_OFFSET CONFIG_HIP_OLED_COLUMN_OFFSET
#define BUTTON_GPIO CONFIG_HIP_BUTTON_GPIO
#define BUTTON_LED_GPIO CONFIG_HIP_BUTTON_LED_GPIO

static const char *TAG = "hip_dash";
static uint8_t fb[OLED_WIDTH * OLED_PAGES];
static bool oled_ready;

enum {
    PLAYER_X = 18,
    PLAYER_W = 12,
    PLAYER_H = 10,
    PLAYER_GROUND_Y100 = 4500,
};

typedef struct {
    int x;
    int w;
    int h;
    bool scored;
} obstacle_t;

typedef struct {
    int player_y100;
    int player_vy100;
    int speed100;
    int score;
    bool alive;
    bool started;
    obstacle_t obstacles[3];
} game_t;

static esp_err_t oled_write(uint8_t control, const uint8_t *data, size_t len)
{
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (OLED_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, control, true);
    if (len > 0) {
        i2c_master_write(cmd, data, len, true);
    }
    i2c_master_stop(cmd);
    esp_err_t err = i2c_master_cmd_begin(I2C_PORT, cmd, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(cmd);
    return err;
}

static esp_err_t oled_cmd(uint8_t cmd)
{
    return oled_write(0x00, &cmd, 1);
}

static bool i2c_probe(uint8_t address)
{
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (address << 1) | I2C_MASTER_WRITE, true);
    i2c_master_stop(cmd);
    esp_err_t err = i2c_master_cmd_begin(I2C_PORT, cmd, pdMS_TO_TICKS(30));
    i2c_cmd_link_delete(cmd);
    return err == ESP_OK;
}

static void i2c_scan(void)
{
    bool found = false;
    for (uint8_t address = 0x08; address <= 0x77; address++) {
        if (i2c_probe(address)) {
            ESP_LOGI(TAG, "I2C device found at 0x%02X", address);
            found = true;
        }
    }
    if (!found) {
        ESP_LOGW(TAG, "No I2C devices found on SDA GPIO %d / SCL GPIO %d", SDA_GPIO, SCL_GPIO);
    }
}

static esp_err_t oled_init(void)
{
    const uint8_t init[] = {
        0xAE, 0xD5, 0x80, 0xA8, 0x3F, 0xD3, 0x00, 0x40,
        0x8D, 0x14, 0x20, 0x00, 0xA1, 0xC8, 0xDA, 0x12,
        0x81, 0xCF, 0xD9, 0xF1, 0xDB, 0x40, 0xA4, 0xA6,
        0xAF
    };

    for (size_t i = 0; i < sizeof(init); i++) {
        ESP_RETURN_ON_ERROR(oled_cmd(init[i]), TAG, "OLED init failed");
    }
    return ESP_OK;
}

static void oled_clear(void)
{
    memset(fb, 0, sizeof(fb));
}

static void oled_flush(void)
{
    if (!oled_ready) {
        return;
    }

    for (uint8_t page = 0; page < OLED_PAGES; page++) {
        uint8_t col = OLED_COL_OFFSET;
        oled_cmd(0xB0 | page);
        oled_cmd(0x00 | (col & 0x0F));
        oled_cmd(0x10 | (col >> 4));

        for (int x = 0; x < OLED_WIDTH; x += 16) {
            oled_write(0x40, &fb[page * OLED_WIDTH + x], 16);
        }
    }
}

static void px(int x, int y, bool on)
{
    if (x < 0 || x >= OLED_WIDTH || y < 0 || y >= OLED_HEIGHT) {
        return;
    }
    uint8_t mask = 1U << (y & 7);
    uint8_t *byte = &fb[(y / 8) * OLED_WIDTH + x];
    if (on) {
        *byte |= mask;
    } else {
        *byte &= (uint8_t)~mask;
    }
}

static void line_h(int x, int y, int w)
{
    for (int i = 0; i < w; i++) {
        px(x + i, y, true);
    }
}

static void fill_rect(int x, int y, int w, int h)
{
    for (int yy = 0; yy < h; yy++) {
        for (int xx = 0; xx < w; xx++) {
            px(x + xx, y + yy, true);
        }
    }
}

static void rect(int x, int y, int w, int h)
{
    line_h(x, y, w);
    line_h(x, y + h - 1, w);
    for (int yy = 1; yy < h - 1; yy++) {
        px(x, y + yy, true);
        px(x + w - 1, y + yy, true);
    }
}

static void draw_dino(int x, int y, bool run_frame)
{
    static const char *frames[2][PLAYER_H] = {
        {
            ".......####.",
            "......######",
            "......##.#..",
            "..#...#####.",
            ".########...",
            "#########...",
            "########....",
            ".##..##.....",
            ".#....##....",
            "##.....#....",
        },
        {
            ".......####.",
            "......######",
            "......##.#..",
            "..#...#####.",
            ".########...",
            "#########...",
            "########....",
            ".##..##.....",
            "##....#.....",
            ".#....##....",
        },
    };

    const char **sprite = frames[run_frame ? 1 : 0];
    for (int yy = 0; yy < PLAYER_H; yy++) {
        for (int xx = 0; xx < PLAYER_W; xx++) {
            if (sprite[yy][xx] == '#') {
                px(x + xx, y + yy, true);
            }
        }
    }
}

static const uint8_t *glyph(char c)
{
    static const uint8_t font[][5] = {
        {0x00,0x00,0x00,0x00,0x00}, /* space */
        {0x3E,0x51,0x49,0x45,0x3E}, {0x00,0x42,0x7F,0x40,0x00},
        {0x42,0x61,0x51,0x49,0x46}, {0x21,0x41,0x45,0x4B,0x31},
        {0x18,0x14,0x12,0x7F,0x10}, {0x27,0x45,0x45,0x45,0x39},
        {0x3C,0x4A,0x49,0x49,0x30}, {0x01,0x71,0x09,0x05,0x03},
        {0x36,0x49,0x49,0x49,0x36}, {0x06,0x49,0x49,0x29,0x1E},
        {0x7E,0x11,0x11,0x11,0x7E}, {0x7F,0x49,0x49,0x49,0x36},
        {0x3E,0x41,0x41,0x41,0x22}, {0x7F,0x41,0x41,0x22,0x1C},
        {0x7F,0x49,0x49,0x49,0x41}, {0x7F,0x09,0x09,0x09,0x01},
        {0x3E,0x41,0x49,0x49,0x7A}, {0x7F,0x08,0x08,0x08,0x7F},
        {0x00,0x41,0x7F,0x41,0x00}, {0x20,0x40,0x41,0x3F,0x01},
        {0x7F,0x08,0x14,0x22,0x41}, {0x7F,0x40,0x40,0x40,0x40},
        {0x7F,0x02,0x0C,0x02,0x7F}, {0x7F,0x04,0x08,0x10,0x7F},
        {0x3E,0x41,0x41,0x41,0x3E}, {0x7F,0x09,0x09,0x09,0x06},
        {0x3E,0x41,0x51,0x21,0x5E}, {0x7F,0x09,0x19,0x29,0x46},
        {0x46,0x49,0x49,0x49,0x31}, {0x01,0x01,0x7F,0x01,0x01},
        {0x3F,0x40,0x40,0x40,0x3F}, {0x1F,0x20,0x40,0x20,0x1F},
        {0x3F,0x40,0x38,0x40,0x3F}, {0x63,0x14,0x08,0x14,0x63},
        {0x07,0x08,0x70,0x08,0x07}, {0x61,0x51,0x49,0x45,0x43}
    };
    if (c == ' ') return font[0];
    if (c >= '0' && c <= '9') return font[1 + c - '0'];
    if (c >= 'A' && c <= 'Z') return font[11 + c - 'A'];
    return font[0];
}

static void text(int x, int y, const char *s)
{
    while (*s) {
        const uint8_t *g = glyph(*s++);
        for (int col = 0; col < 5; col++) {
            for (int row = 0; row < 7; row++) {
                if (g[col] & (1U << row)) {
                    px(x + col, y + row, true);
                }
            }
        }
        x += 6;
    }
}

static bool button_pressed(void)
{
    bool high = gpio_get_level(BUTTON_GPIO);
#if CONFIG_HIP_BUTTON_ACTIVE_LOW
    return !high;
#else
    return high;
#endif
}

static void led_set(bool on)
{
#if BUTTON_LED_GPIO >= 0
    gpio_set_level(BUTTON_LED_GPIO, on ? 1 : 0);
#else
    (void)on;
#endif
}

static void spawn_obstacle(game_t *g, int i, int base_x)
{
    g->obstacles[i].x = base_x + (int)(esp_random() % 28);
    g->obstacles[i].w = 5 + (int)(esp_random() % 5);
    g->obstacles[i].h = 8 + (int)(esp_random() % 14);
    g->obstacles[i].scored = false;
}

static void game_reset(game_t *g)
{
    memset(g, 0, sizeof(*g));
    g->player_y100 = PLAYER_GROUND_Y100;
    g->speed100 = 9500;
    g->alive = true;
    g->started = true;
    spawn_obstacle(g, 0, 148);
    spawn_obstacle(g, 1, 210);
    spawn_obstacle(g, 2, 282);
}

static void game_step(game_t *g, bool jump_edge, int dt_ms)
{
    if (!g->started) {
        if (jump_edge) {
            game_reset(g);
        }
        return;
    }

    if (!g->alive) {
        if (jump_edge) {
            game_reset(g);
        }
        return;
    }

    const int ground_y100 = PLAYER_GROUND_Y100;
    if (jump_edge && g->player_y100 >= ground_y100) {
        g->player_vy100 = -31500;
    }

    g->player_vy100 += 980 * dt_ms / 10;
    g->player_y100 += g->player_vy100 * dt_ms / 1000;
    if (g->player_y100 > ground_y100) {
        g->player_y100 = ground_y100;
        g->player_vy100 = 0;
    }

    int farthest = 128;
    for (int i = 0; i < 3; i++) {
        obstacle_t *o = &g->obstacles[i];
        o->x -= g->speed100 * dt_ms / 100000;
        if (o->x > farthest) {
            farthest = o->x;
        }
        if (!o->scored && o->x + o->w < 18) {
            o->scored = true;
            g->score++;
            if (g->speed100 < 15500) {
                g->speed100 += 220;
            }
        }
        if (o->x + o->w < 0) {
            spawn_obstacle(g, i, farthest + 58);
        }
    }

    int player_x = PLAYER_X;
    int player_y = g->player_y100 / 100;
    for (int i = 0; i < 3; i++) {
        obstacle_t *o = &g->obstacles[i];
        int ox = o->x;
        int oy = 54 - o->h;
        bool hit_x = player_x < ox + o->w && player_x + PLAYER_W > ox;
        bool hit_y = player_y < oy + o->h && player_y + PLAYER_H > oy;
        if (hit_x && hit_y) {
            g->alive = false;
        }
    }
}

static void draw_game(const game_t *g, int64_t now_us)
{
    oled_clear();
    line_h(0, 55, 128);
    for (int x = 0; x < 128; x += 9) {
        px(x, 58, true);
        px(x + 1, 59, true);
    }

    if (!g->started) {
        text(37, 14, "HIP DASH");
        text(24, 34, "PRESS BUTTON");
        oled_flush();
        return;
    }

    char score[16];
    snprintf(score, sizeof(score), "%d", g->score);
    text(2, 2, score);

    int player_y = g->player_y100 / 100;
    bool running = g->alive && g->player_y100 >= PLAYER_GROUND_Y100;
    bool run_frame = running && ((now_us / 130000) % 2 == 0);
    draw_dino(PLAYER_X, player_y, run_frame);

    for (int i = 0; i < 3; i++) {
        const obstacle_t *o = &g->obstacles[i];
        int oy = 54 - o->h;
        fill_rect(o->x, oy, o->w, o->h);
        px(o->x, oy - 1, true);
        px(o->x + o->w - 1, oy - 1, true);
    }

    if (!g->alive) {
        rect(18, 17, 92, 29);
        text(36, 22, "GAME OVER");
        text(35, 34, "PRESS BTN");
    }

    oled_flush();
}

static void hardware_init(void)
{
    gpio_config_t button_conf = {
        .pin_bit_mask = 1ULL << BUTTON_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&button_conf));

#if BUTTON_LED_GPIO >= 0
    gpio_config_t led_conf = {
        .pin_bit_mask = 1ULL << BUTTON_LED_GPIO,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&led_conf));
    led_set(false);
#endif

    i2c_config_t i2c_conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = SDA_GPIO,
        .scl_io_num = SCL_GPIO,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = I2C_FREQ_HZ,
        .clk_flags = 0,
    };
    ESP_ERROR_CHECK(i2c_param_config(I2C_PORT, &i2c_conf));
    ESP_ERROR_CHECK(i2c_driver_install(I2C_PORT, I2C_MODE_MASTER, 0, 0, 0));

    ESP_LOGI(TAG, "OLED config: SDA GPIO %d, SCL GPIO %d, address 0x%02X, column offset %d",
             SDA_GPIO, SCL_GPIO, OLED_ADDR, OLED_COL_OFFSET);
    i2c_scan();

    oled_ready = oled_init() == ESP_OK;
    if (!oled_ready) {
        ESP_LOGE(TAG, "OLED not reachable. Check SDA/SCL wiring, GND/VCC, address 0x3C/0x3D, and column offset.");
    }
}

void app_main(void)
{
    hardware_init();

    game_t game = {
        .player_y100 = PLAYER_GROUND_Y100,
        .alive = true,
        .started = false,
    };

    bool last_button = button_pressed();
    int64_t last_us = esp_timer_get_time();

    while (true) {
        int64_t now_us = esp_timer_get_time();
        int dt_ms = (int)((now_us - last_us) / 1000);
        if (dt_ms < 1) {
            dt_ms = 1;
        } else if (dt_ms > 50) {
            dt_ms = 50;
        }
        last_us = now_us;

        bool current_button = button_pressed();
        bool jump_edge = current_button && !last_button;
        last_button = current_button;

        game_step(&game, jump_edge, dt_ms);

        if (!oled_ready && (now_us / 1000000) != ((now_us - dt_ms * 1000LL) / 1000000)) {
            oled_ready = oled_init() == ESP_OK;
            if (oled_ready) {
                ESP_LOGI(TAG, "OLED recovered");
            }
        }

        draw_game(&game, now_us);

        bool airborne = game.started && game.alive && game.player_y100 < PLAYER_GROUND_Y100;
        bool game_over_blink = game.started && !game.alive && ((now_us / 180000) % 2 == 0);
        bool oled_missing_blink = !oled_ready && ((now_us / 250000) % 2 == 0);
        led_set(current_button || airborne || game_over_blink || oled_missing_blink);

        vTaskDelay(pdMS_TO_TICKS(33));
    }
}
