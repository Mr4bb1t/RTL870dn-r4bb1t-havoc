#ifndef GAMES_H
#define GAMES_H

#include "config.h"
#include <U8g2lib.h>

extern int games_menu_cur_x;
extern int games_menu_target_x;

// Inicialização dos jogos
void init_tetris();
void init_invaders();
void init_doom();
void init_snake();

// Handlers de input com os botões lidos em handle_input()
void handle_tetris_input(bool up, bool dn, bool left, bool right, bool enter);
void handle_invaders_input(bool up, bool dn, bool left, bool right, bool enter);
void handle_doom_input(bool up, bool dn, bool left, bool right, bool enter);
void handle_snake_input(bool up, bool dn, bool left, bool right, bool enter);

// Renderizadores de tela
void render_games_menu();
void render_game_tetris();
void render_game_invaders();
void render_game_doom();
void render_game_snake();

#endif // GAMES_H
