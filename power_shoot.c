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

void generate_enemies(uint32_t enemies[]) {
    size_t num_enemies = (rand() % (MAX_ENEMIES - MIN_ENEMIES + 1)) + MIN_ENEMIES;

    for (size_t i = 0; i < MAX_ENEMIES; i++) {
        enemies[i] = i < num_enemies ? get_terminal_width() + i * 5 : 0;
    }
}

void initialize_game(uint32_t *position, uint32_t enemies[], uint32_t bullets[]) {
    srand(time(NULL));

    *position = 0;
    generate_enemies(enemies);

    for (size_t i = 0; i < MAX_BULLETS; i++) {
        bullets[i] = 0;
    }
}

void print_score(uint32_t score, uint32_t width) {
    printf("\033[37;41m\033[%dG %02d \033[0m", width + 1, score);
}

void draw_game(uint32_t position, uint32_t enemies[], uint32_t bullets[], uint8_t direction, uint32_t score) {
    uint16_t width = get_terminal_width();

    printf("\033[2K");

    print_score(score, width);
    printf("\033[%dG%s", position + 1, direction == 1 ? "🚶‍➡️" : "🚶");

    for (uint32_t i = 0; i < MAX_ENEMIES; i++) {
        if (enemies[i] && enemies[i] < width - 1) {
            printf("\033[%dG👾", enemies[i] + 1);
        }
    }

    for (uint32_t i = 0; i < MAX_BULLETS; i++) {
        if (bullets[i]) {
            printf("\033[%dG💣", bullets[i] + 1);
        }
    }

    fflush(stdout);
}


void move_character(uint32_t *position, int8_t direction) {
    *position = (*position + direction) % get_terminal_width();
}

void move_enemies(uint32_t enemies[]) {
    for (size_t i = 0; i < MAX_ENEMIES; i++) {
        if (enemies[i]) {
            enemies[i]--;
        }
    }
}

void move_bullets(uint32_t bullets[]) {
    for (size_t i = 0; i < MAX_BULLETS; i++) {
        if (bullets[i]) {
            bullets[i]++;
        }
    }
}

bool check_collisions(uint32_t enemies[], uint32_t bullets[], uint32_t position, uint32_t *score) {
    uint16_t width = get_terminal_width();

    for (size_t i = 0; i < MAX_ENEMIES; i++) {
        if (enemies[i] && enemies[i] == position) {
            return true;
        }
    }

    for (size_t i = 0; i < MAX_BULLETS; i++) {
        if (bullets[i]) {
            if (bullets[i] >= width) {
                bullets[i] = 1;
            } else if (bullets[i] == position) {
                return true;
            }

            for (size_t j = 0; j < MAX_ENEMIES; j++) {
                if (enemies[j]) {
                    if (bullets[i] == enemies[j]) {
                        enemies[j] = 0;
                        bullets[i] = 0;
                        (*score)++;
                        break;
                    }
                }
            }
        }
    }

    return false;
}

void fire(uint32_t bullets[MAX_BULLETS], uint32_t position) {
    for (size_t i = 0; i < MAX_BULLETS; i++) {
        if (bullets[i] == position + 1) {
            return;
        }
    }

    for (size_t i = 0; i < MAX_BULLETS; i++) {
        if (!bullets[i]) {
            bullets[i] = position + 2;
            break;
        }
    }    
}

bool fire_if_ac(bool prev_state, uint32_t bullets[MAX_BULLETS], uint32_t position) {
    bool state = is_ac_power();
    if (state && !prev_state) {
        fire(bullets, position);
    }

    return state;
}

bool check_if_all_enemies_destroyed(uint32_t enemies[]) {
    for (size_t i = 0; i < MAX_ENEMIES; i++) {
        if (enemies[i] != 0) {
            return false;
        }
    }
    return true;
}

uint32_t game_loop(void) {
    bool ac_state = false;
    int8_t direction = 1;

    uint32_t position, enemies[MAX_ENEMIES], bullets[MAX_BULLETS], score = 0;

    initialize_game(&position, enemies, bullets);
    draw_game(position, enemies, bullets, direction, score);

    for (;;) {
        move_character(&position, direction);

        if (position == 0) {
            generate_enemies(enemies);
            direction = 1;
        }

        if (check_collisions(enemies, bullets, position, &score)) {
            return position;
        }        

        move_enemies(enemies);

        for (size_t i = 0; i < 2; i++) {
            if (check_collisions(enemies, bullets, position, &score)) {
                return position;
            }

            ac_state = fire_if_ac(ac_state, bullets, position);
            move_bullets(bullets);

            if (check_collisions(enemies, bullets, position, &score)) {
                return position;
            }

            if (direction > 0 && check_if_all_enemies_destroyed(enemies)) {
                if (position < get_terminal_width() / 2) {
                    direction = -1;
                }
            }

            draw_game(position, enemies, bullets, direction, score);

            usleep(50000);
        }
    }
}

int main(void) {
    signal(SIGINT, handle_sigint);
    printf("\033[?25l");

    printf("\033[?25h\033[%dG🪦", game_loop());
    return 0;
}
