#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <signal.h>
#include <fcntl.h>
#include <stdint.h>
#include <time.h>
#include <stdbool.h>
#include <IOKit/ps/IOPowerSources.h>

#define MAX_ENEMIES 2
#define MIN_ENEMIES 1
#define MAX_BULLETS 255
#define MAX_WIDTH 80
#define SCORE_WIDTH 5

typedef struct {
    uint32_t position;
    uint32_t enemies[MAX_ENEMIES];
    uint32_t bullets[MAX_BULLETS];
    uint32_t score;
    int8_t direction;
    bool ac_state;
} GameState;

bool is_ac_power(void) {
    CFTypeRef powerSourceInfo = IOPSCopyPowerSourcesInfo();
    if (powerSourceInfo == NULL) {
        return false;
    }

    CFStringRef powerSourceType = IOPSGetProvidingPowerSourceType(powerSourceInfo);
    if (powerSourceType == NULL) {
        CFRelease(powerSourceInfo);
        return false;
    }

    bool result = CFStringCompare(powerSourceType, CFSTR(kIOPMACPowerKey), 0) == kCFCompareEqualTo;
    CFRelease(powerSourceInfo);
    return result;
}

uint16_t get_terminal_width(void) {
    struct winsize w;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) == -1) {
        exit(EXIT_FAILURE);
    }

    uint16_t width = w.ws_col - SCORE_WIDTH;
    return width > MAX_WIDTH ? MAX_WIDTH : width;
}

void handle_sigint(int sig) {
    printf("\033[?25h\033[0m");
    exit(EXIT_SUCCESS);
}

void generate_enemies(GameState *ctx) {
    size_t num_enemies = (rand() % (MAX_ENEMIES - MIN_ENEMIES + 1)) + MIN_ENEMIES;
    uint16_t width = get_terminal_width();

    for (size_t i = 0; i < MAX_ENEMIES; i++) {
        ctx->enemies[i] = i < num_enemies ? width + i * 5 : 0;
    }
}

void initialize_game(GameState *ctx) {
    srand(time(NULL));

    ctx->position = 0;
    generate_enemies(ctx);

    for (size_t i = 0; i < MAX_BULLETS; i++) {
        ctx->bullets[i] = 0;
    }

    ctx->ac_state = false;
    ctx->direction = 1;
    ctx->score = 0;
}

void print_score(GameState *ctx, uint32_t width) {
    printf("\033[37;41m\033[%dG %02d \033[0m", width + 1, ctx->score);
}

void draw_game(GameState *ctx) {
    uint16_t width = get_terminal_width();

    printf("\033[2K");

    print_score(ctx, width);
    printf("\033[%dG%s", ctx->position + 1, ctx->direction == 1 ? "🚶‍➡️" : "🚶");

    for (uint32_t i = 0; i < MAX_ENEMIES; i++) {
        if (ctx->enemies[i] && ctx->enemies[i] < width - 1) {
            printf("\033[%dG👾", ctx->enemies[i] + 1);
        }
    }

    for (uint32_t i = 0; i < MAX_BULLETS; i++) {
        if (ctx->bullets[i]) {
            printf("\033[%dG💣", ctx->bullets[i] + 1);
        }
    }

    fflush(stdout);
}


void move_character(GameState *ctx) {
    ctx->position = (ctx->position + ctx->direction) % get_terminal_width();
}

void move_enemies(GameState *ctx) {
    for (size_t i = 0; i < MAX_ENEMIES; i++) {
        if (ctx->enemies[i]) {
            ctx->enemies[i]--;
        }
    }
}

void move_bullets(GameState *ctx) {
    for (size_t i = 0; i < MAX_BULLETS; i++) {
        if (ctx->bullets[i]) {
            ctx->bullets[i]++;
        }
    }
}

bool check_collisions(GameState *ctx) {
    uint16_t width = get_terminal_width();

    for (size_t i = 0; i < MAX_ENEMIES; i++) {
        if (ctx->enemies[i] && ctx->enemies[i] == ctx->position) {
            return true;
        }
    }

    for (size_t i = 0; i < MAX_BULLETS; i++) {
        if (ctx->bullets[i]) {
            if (ctx->bullets[i] >= width) {
                ctx->bullets[i] = 1;
            } else if (ctx->bullets[i] == ctx->position) {
                return true;
            }

            for (size_t j = 0; j < MAX_ENEMIES; j++) {
                if (ctx->enemies[j]) {
                    if (ctx->bullets[i] == ctx->enemies[j]) {
                        ctx->enemies[j] = 0;
                        ctx->bullets[i] = 0;
                        ctx->score++;
                        break;
                    }
                }
            }
        }
    }

    return false;
}

void fire(GameState *ctx) {
    for (size_t i = 0; i < MAX_BULLETS; i++) {
        if (ctx->bullets[i] == ctx->position + 2) {
            return;
        }
    }

    for (size_t i = 0; i < MAX_BULLETS; i++) {
        if (!ctx->bullets[i]) {
            ctx->bullets[i] = ctx->position + 2;
            break;
        }
    }    
}

void fire_if_ac(GameState *ctx) {
    bool new_state = is_ac_power();
    if (new_state && !ctx->ac_state) {
        fire(ctx);
    }

    ctx->ac_state = new_state;
}

bool check_if_all_enemies_destroyed(GameState *ctx) {
    for (size_t i = 0; i < MAX_ENEMIES; i++) {
        if (ctx->enemies[i] != 0) {
            return false;
        }
    }
    return true;
}

void game_loop(GameState *ctx) {
    initialize_game(ctx);
    draw_game(ctx);

    for (;;) {
        move_character(ctx);

        if (ctx->position == 0) {
            generate_enemies(ctx);
            ctx->direction = 1;
        }

        if (check_collisions(ctx)) {
            return;
        }        

        move_enemies(ctx);

        for (size_t i = 0; i < 2; i++) {
            if (check_collisions(ctx)) {
                return;
            }

            fire_if_ac(ctx);
            move_bullets(ctx);

            if (check_collisions(ctx)) {
                return;
            }

            if (ctx->direction > 0 && check_if_all_enemies_destroyed(ctx)) {
                if (ctx->position < get_terminal_width() / 2) {
                    ctx->direction = -1;
                }
            }

            draw_game(ctx);

            usleep(50000);
        }
    }
}

int main(void) {
    GameState ctx;

    signal(SIGINT, handle_sigint);
    printf("\033[?25l");

    game_loop(&ctx);

    printf("\033[?25h\033[%dG🪦", ctx.position);
    return 0;
}
