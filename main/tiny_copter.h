#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    DIFFICULTY_EASY,
    DIFFICULTY_NORMAL,
    DIFFICULTY_HARD,
} difficulty_t;

typedef struct {
    void (*px)(int x, int y, bool on);
    void (*line_h)(int x, int y, int w);
    void (*fill_rect)(int x, int y, int w, int h);
    void (*rect)(int x, int y, int w, int h);
    void (*text)(int x, int y, const char *s);
    void (*clear)(void);
    void (*flush)(void);
} tiny_draw_api_t;

typedef struct {
    int x;
    int gap_y;
    int gap_h;
    int w;
    bool scored;
} tiny_wall_t;

typedef struct {
    int copter_y100;
    int copter_vy100;
    int speed100;
    int score;
    int high_score;
    bool alive;
    bool started;
    bool high_score_saved;
    int grace_ms;
    difficulty_t difficulty;
    tiny_wall_t walls[3];
} tiny_copter_t;

void tiny_copter_init(tiny_copter_t *game, int high_score, difficulty_t difficulty);
void tiny_copter_reset(tiny_copter_t *game);
void tiny_copter_step(tiny_copter_t *game, bool button_down, bool start_edge, int dt_ms);
void tiny_copter_draw(const tiny_copter_t *game, const tiny_draw_api_t *draw, int64_t now_us);
