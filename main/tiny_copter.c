#include "tiny_copter.h"

#include <stdio.h>
#include <string.h>

#include "esp_random.h"

enum {
    COPTER_X = 18,
    COPTER_W = 15,
    COPTER_H = 7,
    COPTER_START_Y100 = 2600,
};

static int start_speed(difficulty_t difficulty)
{
    switch (difficulty) {
    case DIFFICULTY_EASY:
        return 3200;
    case DIFFICULTY_HARD:
        return 5200;
    case DIFFICULTY_NORMAL:
    default:
        return 3900;
    }
}

static int speed_limit(difficulty_t difficulty)
{
    switch (difficulty) {
    case DIFFICULTY_EASY:
        return 5600;
    case DIFFICULTY_HARD:
        return 8400;
    case DIFFICULTY_NORMAL:
    default:
        return 6800;
    }
}

static int gap_height(difficulty_t difficulty)
{
    switch (difficulty) {
    case DIFFICULTY_EASY:
        return 38;
    case DIFFICULTY_HARD:
        return 30;
    case DIFFICULTY_NORMAL:
    default:
        return 35;
    }
}

static void spawn_wall(tiny_copter_t *game, int index, int base_x)
{
    int gap_h = gap_height(game->difficulty);
    int min_y = 11;
    int max_y = 52 - gap_h;
    uint32_t random = esp_random();
    game->walls[index].x = base_x + (int)(random % 24);
    game->walls[index].gap_y = min_y + (int)((random >> 8) % (uint32_t)(max_y - min_y + 1));
    game->walls[index].gap_h = gap_h;
    game->walls[index].w = 8;
    game->walls[index].scored = false;
}

void tiny_copter_init(tiny_copter_t *game, int high_score, difficulty_t difficulty)
{
    memset(game, 0, sizeof(*game));
    game->high_score = high_score;
    game->difficulty = difficulty;
    game->copter_y100 = COPTER_START_Y100;
    game->alive = true;
}

void tiny_copter_reset(tiny_copter_t *game)
{
    int high_score = game->high_score;
    difficulty_t difficulty = game->difficulty;
    memset(game, 0, sizeof(*game));
    game->high_score = high_score;
    game->difficulty = difficulty;
    game->copter_y100 = COPTER_START_Y100;
    game->speed100 = start_speed(difficulty);
    game->alive = true;
    game->started = true;
    game->grace_ms = 900;
    spawn_wall(game, 0, 230);
    spawn_wall(game, 1, 326);
    spawn_wall(game, 2, 422);
}

void tiny_copter_step(tiny_copter_t *game, bool button_down, bool start_edge, int dt_ms)
{
    if (!game->started) {
        if (start_edge) {
            tiny_copter_reset(game);
        }
        return;
    }

    if (!game->alive) {
        if (start_edge) {
            tiny_copter_reset(game);
        }
        return;
    }

    int accel = button_down ? -310 : 260;
    game->copter_vy100 += accel * dt_ms / 10;
    if (game->copter_vy100 < -4250) {
        game->copter_vy100 = -4250;
    } else if (game->copter_vy100 > 4250) {
        game->copter_vy100 = 4250;
    }
    game->copter_y100 += game->copter_vy100 * dt_ms / 1000;
    if (game->grace_ms > 0) {
        game->grace_ms -= dt_ms;
    }

    int copter_y = game->copter_y100 / 100;
    if (game->grace_ms <= 0 && (copter_y < 9 || copter_y + COPTER_H > 54)) {
        game->alive = false;
        if (game->score > game->high_score) {
            game->high_score = game->score;
            game->high_score_saved = true;
        }
        return;
    }

    int farthest = 128;
    for (int i = 0; i < 3; i++) {
        tiny_wall_t *wall = &game->walls[i];
        wall->x -= game->speed100 * dt_ms / 100000;
        if (wall->x > farthest) {
            farthest = wall->x;
        }
        if (!wall->scored && wall->x + wall->w < COPTER_X) {
            wall->scored = true;
            game->score++;
            if (game->speed100 < speed_limit(game->difficulty)) {
                game->speed100 += 70;
            }
        }
        if (wall->x + wall->w < 0) {
            spawn_wall(game, i, farthest + 88);
        }
    }

    for (int i = 0; i < 3; i++) {
        const tiny_wall_t *wall = &game->walls[i];
        bool hit_x = COPTER_X < wall->x + wall->w && COPTER_X + COPTER_W > wall->x;
        bool hit_top = copter_y < wall->gap_y;
        bool hit_bottom = copter_y + COPTER_H > wall->gap_y + wall->gap_h;
        if (game->grace_ms <= 0 && hit_x && (hit_top || hit_bottom)) {
            game->alive = false;
            if (game->score > game->high_score) {
                game->high_score = game->score;
                game->high_score_saved = true;
            }
            return;
        }
    }
}

static void draw_copter(const tiny_draw_api_t *draw, int x, int y, int64_t now_us)
{
    bool blade = ((now_us / 70000) % 2) == 0;
    if (blade) {
        draw->line_h(x + 1, y - 2, 13);
        draw->px(x + 7, y - 3, true);
    } else {
        draw->line_h(x + 4, y - 3, 7);
        draw->line_h(x + 2, y - 2, 11);
    }
    draw->fill_rect(x + 3, y + 1, 9, 4);
    draw->px(x + 12, y + 2, true);
    draw->px(x + 13, y + 2, true);
    draw->line_h(x, y + 3, 3);
    draw->line_h(x + 4, y + 6, 8);
    draw->px(x + 6, y + 2, false);
}

void tiny_copter_draw(const tiny_copter_t *game, const tiny_draw_api_t *draw, int64_t now_us)
{
    draw->clear();
    draw->line_h(0, 8, 128);
    draw->line_h(0, 55, 128);

    if (!game->started) {
        draw->text(29, 12, "TINY COPTER");
        draw->text(22, 28, "PRESS HOLD");
        char high_score[16];
        snprintf(high_score, sizeof(high_score), "HI %d", game->high_score);
        draw->text(47, 43, high_score);
        draw->flush();
        return;
    }

    char score[16];
    snprintf(score, sizeof(score), "%d", game->score);
    draw->text(2, 0, score);

    char high_score[16];
    snprintf(high_score, sizeof(high_score), "HI %d", game->high_score);
    draw->text(82, 0, high_score);

    for (int i = 0; i < 3; i++) {
        const tiny_wall_t *wall = &game->walls[i];
        draw->fill_rect(wall->x, 9, wall->w, wall->gap_y - 9);
        draw->fill_rect(wall->x, wall->gap_y + wall->gap_h, wall->w,
                        55 - (wall->gap_y + wall->gap_h));
        draw->line_h(wall->x - 1, wall->gap_y, wall->w + 2);
        draw->line_h(wall->x - 1, wall->gap_y + wall->gap_h - 1, wall->w + 2);
    }

    draw_copter(draw, COPTER_X, game->copter_y100 / 100, now_us);

    if (game->grace_ms > 0 && ((now_us / 160000) % 2) == 0) {
        draw->text(50, 28, "GO");
    }

    if (!game->alive) {
        draw->rect(18, 17, 92, 29);
        draw->text(36, 22, "GAME OVER");
        draw->text(35, 34, "PRESS BTN");
    }

    draw->flush();
}
