#line 1 "C:\\Users\\R4bb1t\\Downloads\\RTL870dn-r4bb1t-havoc-main\\RTL870dn-r4bb1t-havoc-main\\RTL8720dn-Deauther\\games.cpp"
// STL MUST come first
#undef max
#undef min
#undef abs
#include <vector>
#include <cmath>
#include <cstdlib>

#include "games.h"
#include "ui_display.h"
#include "ui_state.h"
#include "config.h"
#include "debug.h"
#include "doom_assets.h"

// =============================================================================
// SUBMENU DE JOGOS (GAMES CAROUSEL)
// =============================================================================
const char* GAMES_MENU[] = {"Tetris", "Space Invaders", "DOOM 3D", "Snake", "< Back"};

int games_menu_cur_x    = 0;
int games_menu_target_x = 0;

void render_games_menu() {
  const char *badges[] = {"PUZZLE", "SHOOTER", "3D RAYCASTER", "ARCADE", "NAVIGATION"};
  const char *titles[] = {"TETRIS", "INVADERS", "DOOM 3D", "SNAKE", "RETURN"};

  for (int i = 0; i < GAMES_MENU_LEN; i++) {
    int cx = (i * 128) - games_menu_cur_x;
    if (cx < -124 || cx > 124)
      continue;

    // Card boundary (112 px wide, 50 px high, centered at cx + 8, Y=4..54)
    display.drawRFrame(cx + 8, 4, 112, 50, 3);

    // Top Header Badge
    display.setFont(u8g2_font_5x7_tf);
    int bw = display.getStrWidth(badges[i]);
    if (i == menu_sel) {
      display.drawBox(cx + 9, 5, 110, 11);
      display.setDrawColor(0);
      display.drawStr(cx + 8 + (112 - bw) / 2, 13, badges[i]);
      display.setDrawColor(1);
    } else {
      display.drawHLine(cx + 9, 16, 110);
      display.drawStr(cx + 8 + (112 - bw) / 2, 13, badges[i]);
    }

    // Title in bold
    display.setFont(u8g2_font_7x14B_tf);
    display.drawStr(cx + 14, 33, titles[i]);

    // Subtitle below title
    display.setFont(u8g2_font_5x7_tf);
    if (i == 0) {
      display.drawStr(cx + 14, 46, "Stack & Clear >");
    } else if (i == 1) {
      display.drawStr(cx + 14, 46, "Defend Earth >");
    } else if (i == 2) {
      display.drawStr(cx + 14, 46, "Rip & Tear >");
    } else if (i == 3) {
      display.drawStr(cx + 14, 46, "Eat & Grow >");
    } else {
      display.drawStr(cx + 14, 46, "< Back to Menu");
    }

    // Right-side vector graphics for each card
    if (i == 0) {
      // Tetris: T-tetromino and square blocks
      int bx = cx + 94, by = 25;
      display.drawBox(bx, by, 6, 6);
      display.drawBox(bx + 6, by, 6, 6);
      display.drawBox(bx + 12, by, 6, 6);
      display.drawBox(bx + 6, by + 6, 6, 6);
      display.drawFrame(bx + 12, by + 12, 6, 6);
      display.drawFrame(bx + 6, by + 12, 6, 6);
    } else if (i == 1) {
      // Space Invader 8-bit alien silhouette
      int ax = cx + 92, ay = 25;
      display.drawBox(ax + 3, ay, 8, 2);
      display.drawBox(ax + 1, ay + 2, 12, 4);
      display.drawBox(ax, ay + 6, 14, 5);
      display.drawBox(ax + 2, ay + 11, 3, 3);
      display.drawBox(ax + 9, ay + 11, 3, 3);
      display.setDrawColor(0);
      display.drawBox(ax + 3, ay + 4, 2, 2);
      display.drawBox(ax + 9, ay + 4, 2, 2);
      display.setDrawColor(1);
    } else if (i == 2) {
      // DOOM 3D: Perspective corridor & shotgun crosshair
      int dx = cx + 88, dy = 22;
      display.drawFrame(dx, dy, 26, 20);
      display.drawFrame(dx + 6, dy + 5, 14, 10);
      display.drawLine(dx, dy, dx + 6, dy + 5);
      display.drawLine(dx + 25, dy, dx + 19, dy + 5);
      display.drawLine(dx, dy + 19, dx + 6, dy + 14);
      display.drawLine(dx + 25, dy + 19, dx + 19, dy + 14);
      display.drawDisc(dx + 13, dy + 10, 1);
    } else if (i == 3) {
      // Snake: pixel snake & apple
      int sx = cx + 90, sy = 24;
      display.drawRBox(sx, sy, 16, 4, 1);
      display.drawRBox(sx + 12, sy + 4, 4, 10, 1);
      display.drawRBox(sx + 4, sy + 10, 8, 4, 1);
      display.drawDisc(sx + 2, sy + 11, 2); // Apple
      display.drawPixel(sx + 2, sy + 8);   // Apple stem
    } else {
      // Return Arrow
      display.drawTriangle(cx + 89, 32, cx + 97, 25, cx + 97, 39);
      display.drawBox(cx + 97, 30, 12, 5);
      display.drawBox(cx + 106, 24, 3, 11);
    }
  }

  // Bottom Pagination Pills & Indicators
  display.setFont(u8g2_font_5x7_tf);
  if (menu_sel > 0)
    display.drawStr(2, 61, "<");
  if (menu_sel < GAMES_MENU_LEN - 1)
    display.drawStr(122, 61, ">");

  for (int p = 0; p < GAMES_MENU_LEN; p++) {
    int dot_x = 42 + p * 10;
    if (p == menu_sel) {
      display.drawRBox(dot_x - 1, 58, 8, 3, 1);
    } else {
      display.drawDisc(dot_x + 2, 59, 1);
    }
  }
}

// =============================================================================
// JOGO 1: TETRIS
// =============================================================================
#define TETRIS_ROWS 15
#define TETRIS_COLS 10

static uint8_t tetris_board[TETRIS_ROWS][TETRIS_COLS];

const int8_t TETRO_OFFSETS[7][4][4][2] = {
  { // Shape 0: I
    {{0,1}, {1,1}, {2,1}, {3,1}},
    {{2,0}, {2,1}, {2,2}, {2,3}},
    {{0,2}, {1,2}, {2,2}, {3,2}},
    {{1,0}, {1,1}, {1,2}, {1,3}},
  },
  { // Shape 1: O
    {{1,0}, {2,0}, {1,1}, {2,1}},
    {{1,0}, {2,0}, {1,1}, {2,1}},
    {{1,0}, {2,0}, {1,1}, {2,1}},
    {{1,0}, {2,0}, {1,1}, {2,1}},
  },
  { // Shape 2: T
    {{1,0}, {0,1}, {1,1}, {2,1}},
    {{1,0}, {1,1}, {2,1}, {1,2}},
    {{0,1}, {1,1}, {2,1}, {1,2}},
    {{1,0}, {0,1}, {1,1}, {1,2}},
  },
  { // Shape 3: S
    {{1,0}, {2,0}, {0,1}, {1,1}},
    {{1,0}, {1,1}, {2,1}, {2,2}},
    {{1,1}, {2,1}, {0,2}, {1,2}},
    {{0,0}, {0,1}, {1,1}, {1,2}},
  },
  { // Shape 4: Z
    {{0,0}, {1,0}, {1,1}, {2,1}},
    {{2,0}, {1,1}, {2,1}, {1,2}},
    {{0,1}, {1,1}, {1,2}, {2,2}},
    {{1,0}, {0,1}, {1,1}, {0,2}},
  },
  { // Shape 5: J
    {{0,0}, {0,1}, {1,1}, {2,1}},
    {{1,0}, {2,0}, {1,1}, {1,2}},
    {{0,1}, {1,1}, {2,1}, {2,2}},
    {{1,0}, {1,1}, {0,2}, {1,2}},
  },
  { // Shape 6: L
    {{2,0}, {0,1}, {1,1}, {2,1}},
    {{1,0}, {1,1}, {1,2}, {2,2}},
    {{0,1}, {1,1}, {2,1}, {0,2}},
    {{0,0}, {1,0}, {1,1}, {1,2}},
  },
};

static int  tetris_px = 3, tetris_py = 0;
static int  tetris_shape = 0, tetris_rot = 0;
static int  tetris_next_shape = 0;
static int  tetris_score = 0;
static int  tetris_lines = 0;
static int  tetris_level = 1;
static bool tetris_game_over = false;
static uint32_t tetris_last_drop = 0;

static bool tetris_collides(int px, int py, int shape, int rot) {
  for (int i = 0; i < 4; i++) {
    int x = px + TETRO_OFFSETS[shape][rot][i][0];
    int y = py + TETRO_OFFSETS[shape][rot][i][1];
    if (x < 0 || x >= TETRIS_COLS || y >= TETRIS_ROWS) return true;
    if (y >= 0 && tetris_board[y][x]) return true;
  }
  return false;
}

static void tetris_spawn_piece() {
  tetris_shape = tetris_next_shape;
  tetris_next_shape = rand() % 7;
  tetris_rot   = 0;
  tetris_px    = 3;
  tetris_py    = 0;
  if (tetris_collides(tetris_px, tetris_py, tetris_shape, tetris_rot)) {
    tetris_game_over = true;
  }
}

static void tetris_clear_lines() {
  int cleared = 0;
  for (int y = TETRIS_ROWS - 1; y >= 0; y--) {
    bool full = true;
    for (int x = 0; x < TETRIS_COLS; x++) {
      if (!tetris_board[y][x]) { full = false; break; }
    }
    if (full) {
      cleared++;
      for (int ny = y; ny > 0; ny--) {
        for (int x = 0; x < TETRIS_COLS; x++) {
          tetris_board[ny][x] = tetris_board[ny - 1][x];
        }
      }
      for (int x = 0; x < TETRIS_COLS; x++) {
        tetris_board[0][x] = 0;
      }
      y++; // recheck same row index
    }
  }

  if (cleared > 0) {
    tetris_lines += cleared;
    if (cleared == 1) tetris_score += 100 * tetris_level;
    else if (cleared == 2) tetris_score += 300 * tetris_level;
    else if (cleared == 3) tetris_score += 500 * tetris_level;
    else if (cleared >= 4) tetris_score += 800 * tetris_level;
    tetris_level = (tetris_lines / 10) + 1;
  }
}

static void tetris_lock_piece() {
  for (int i = 0; i < 4; i++) {
    int x = tetris_px + TETRO_OFFSETS[tetris_shape][tetris_rot][i][0];
    int y = tetris_py + TETRO_OFFSETS[tetris_shape][tetris_rot][i][1];
    if (y >= 0 && y < TETRIS_ROWS && x >= 0 && x < TETRIS_COLS) {
      tetris_board[y][x] = 1;
    }
  }
  tetris_clear_lines();
  tetris_spawn_piece();
}

void init_tetris() {
  for (int y = 0; y < TETRIS_ROWS; y++) {
    for (int x = 0; x < TETRIS_COLS; x++) {
      tetris_board[y][x] = 0;
    }
  }
  tetris_score = 0;
  tetris_lines = 0;
  tetris_level = 1;
  tetris_game_over = false;
  tetris_next_shape = rand() % 7;
  tetris_spawn_piece();
  tetris_last_drop = millis();
}

void render_game_tetris() {
  // Atualiza descida automática
  uint32_t drop_interval = 700 - (tetris_level - 1) * 60;
  if (drop_interval < 150) drop_interval = 150;
  if (!tetris_game_over && millis() - tetris_last_drop > drop_interval) {
    if (!tetris_collides(tetris_px, tetris_py + 1, tetris_shape, tetris_rot)) {
      tetris_py++;
    } else {
      tetris_lock_piece();
    }
    tetris_last_drop = millis();
  }

  // Desenha Moldura Ampliada do Tabuleiro (X=2, Y=1, W=42, H=62)
  display.drawFrame(2, 1, 42, 62);

  // Ghost piece (sombra translúcida de onde a peça vai cair)
  int ghost_y = tetris_py;
  while (!tetris_collides(tetris_px, ghost_y + 1, tetris_shape, tetris_rot)) {
    ghost_y++;
  }
  if (!tetris_game_over && ghost_y != tetris_py) {
    for (int i = 0; i < 4; i++) {
      int gx = tetris_px + TETRO_OFFSETS[tetris_shape][tetris_rot][i][0];
      int gy = ghost_y + TETRO_OFFSETS[tetris_shape][tetris_rot][i][1];
      if (gy >= 0 && gy < TETRIS_ROWS && gx >= 0 && gx < TETRIS_COLS) {
        display.drawFrame(3 + gx * 4, 2 + gy * 4, 3, 3);
        display.drawPixel(3 + gx * 4 + 1, 2 + gy * 4 + 1);
      }
    }
  }

  // Peças fixadas no tabuleiro (blocos 3x3 sólidos com 1px de espaçamento)
  for (int y = 0; y < TETRIS_ROWS; y++) {
    for (int x = 0; x < TETRIS_COLS; x++) {
      if (tetris_board[y][x]) {
        display.drawBox(3 + x * 4, 2 + y * 4, 3, 3);
      }
    }
  }

  // Peça caindo atual
  if (!tetris_game_over) {
    for (int i = 0; i < 4; i++) {
      int px = tetris_px + TETRO_OFFSETS[tetris_shape][tetris_rot][i][0];
      int py = tetris_py + TETRO_OFFSETS[tetris_shape][tetris_rot][i][1];
      if (py >= 0 && py < TETRIS_ROWS && px >= 0 && px < TETRIS_COLS) {
        display.drawBox(3 + px * 4, 2 + py * 4, 3, 3);
      }
    }
  }

  // Painel Lateral (X=48..127)
  display.setFont(u8g2_font_7x14B_tf);
  display.drawStr(48, 12, "TETRIS");

  // Caixa da Próxima Peça
  display.drawFrame(48, 15, 24, 20);
  for (int i = 0; i < 4; i++) {
    int nx = TETRO_OFFSETS[tetris_next_shape][0][i][0];
    int ny = TETRO_OFFSETS[tetris_next_shape][0][i][1];
    display.drawBox(50 + nx * 4, 17 + ny * 4, 3, 3);
  }

  // Estatísticas
  display.setFont(u8g2_font_5x7_tf);
  char buf[20];
  display.drawStr(76, 22, "SCORE");
  snprintf(buf, sizeof(buf), "%d", tetris_score);
  display.drawStr(76, 30, buf);

  snprintf(buf, sizeof(buf), "LINES: %d", tetris_lines);
  display.drawStr(48, 43, buf);
  snprintf(buf, sizeof(buf), "LVL: %d", tetris_level);
  display.drawStr(48, 52, buf);

  display.setFont(u8g2_font_4x6_tf);
  display.drawStr(46, 62, "MID:Gira D:Queda");

  // Game Over Overlay
  if (tetris_game_over) {
    display.drawBox(8, 18, 112, 28);
    display.setDrawColor(0);
    display.drawFrame(10, 20, 108, 24);
    display.setFont(u8g2_font_7x14B_tf);
    display.drawStr(22, 33, "GAME OVER");
    display.setFont(u8g2_font_5x7_tf);
    display.drawStr(14, 42, "ENT:Replay  LFT:Exit");
    display.setDrawColor(1);
  }
}

// =============================================================================
// JOGO 2: SPACE INVADERS
// =============================================================================
#define INVADERS_ROWS 3
#define INVADERS_COLS 6

static bool invaders_alive[INVADERS_ROWS][INVADERS_COLS];
static float invaders_fleet_x = 8.0f;
static float invaders_fleet_y = 12.0f;
static float invaders_fleet_dir = 1.0f;
static uint32_t invaders_last_step = 0;
static bool invaders_anim_frame = false;

static int   invaders_player_x = 58;
static int   invaders_lives = 3;
static int   invaders_score = 0;
static bool  invaders_game_over = false;
static bool  invaders_victory = false;

struct InvaderBullet {
  float x, y;
  bool active;
};
#define MAX_PLAYER_BULLETS 3
#define MAX_ALIEN_BOMBS 2
static InvaderBullet player_bullets[MAX_PLAYER_BULLETS];
static InvaderBullet alien_bombs[MAX_ALIEN_BOMBS];

// 3 Bunkers com vida
static int invaders_bunkers[3] = {3, 3, 3};

void init_invaders() {
  for (int r = 0; r < INVADERS_ROWS; r++) {
    for (int c = 0; c < INVADERS_COLS; c++) {
      invaders_alive[r][c] = true;
    }
  }
  invaders_fleet_x = 8.0f;
  invaders_fleet_y = 12.0f;
  invaders_fleet_dir = 1.0f;
  invaders_anim_frame = false;
  invaders_player_x = 58;
  invaders_lives = 3;
  invaders_score = 0;
  invaders_game_over = false;
  invaders_victory = false;
  invaders_last_step = millis();

  for (int i = 0; i < MAX_PLAYER_BULLETS; i++) player_bullets[i].active = false;
  for (int i = 0; i < MAX_ALIEN_BOMBS; i++) alien_bombs[i].active = false;
  invaders_bunkers[0] = invaders_bunkers[1] = invaders_bunkers[2] = 3;
}

void render_game_invaders() {
  uint32_t now = millis();

  // Conta aliens vivos
  int alive_count = 0;
  int min_col = 99, max_col = -1;
  float lowest_y = 0;
  for (int r = 0; r < INVADERS_ROWS; r++) {
    for (int c = 0; c < INVADERS_COLS; c++) {
      if (invaders_alive[r][c]) {
        alive_count++;
        if (c < min_col) min_col = c;
        if (c > max_col) max_col = c;
        float ay = invaders_fleet_y + r * 8;
        if (ay > lowest_y) lowest_y = ay;
      }
    }
  }

  if (alive_count == 0) {
    invaders_victory = true;
    invaders_game_over = true;
  }

  // Atualização dos Aliens
  uint32_t step_interval = 200 + alive_count * 30;
  if (!invaders_game_over && now - invaders_last_step > step_interval) {
    invaders_anim_frame = !invaders_anim_frame;
    float next_x = invaders_fleet_x + invaders_fleet_dir * 3.0f;

    // Checa limites da tela
    if (next_x + max_col * 14 > 122 || next_x + min_col * 14 < 4) {
      invaders_fleet_dir = -invaders_fleet_dir;
      invaders_fleet_y += 3.0f;
    } else {
      invaders_fleet_x = next_x;
    }

    if (lowest_y >= 48.0f) {
      invaders_game_over = true;
    }

    invaders_last_step = now;

    // Alien dispara bomba aleatoriamente
    for (int i = 0; i < MAX_ALIEN_BOMBS; i++) {
      if (!alien_bombs[i].active && (rand() % 4 == 0)) {
        int rand_c = rand() % INVADERS_COLS;
        for (int r = INVADERS_ROWS - 1; r >= 0; r--) {
          if (invaders_alive[r][rand_c]) {
            alien_bombs[i].x = invaders_fleet_x + rand_c * 14 + 4;
            alien_bombs[i].y = invaders_fleet_y + r * 8 + 6;
            alien_bombs[i].active = true;
            break;
          }
        }
      }
    }
  }

  // Atualiza tiros do jogador
  for (int i = 0; i < MAX_PLAYER_BULLETS; i++) {
    if (player_bullets[i].active) {
      player_bullets[i].y -= 3.0f;
      if (player_bullets[i].y < 8) {
        player_bullets[i].active = false;
        continue;
      }

      // Colisão com Aliens
      for (int r = 0; r < INVADERS_ROWS; r++) {
        for (int c = 0; c < INVADERS_COLS; c++) {
          if (invaders_alive[r][c]) {
            float ax = invaders_fleet_x + c * 14;
            float ay = invaders_fleet_y + r * 8;
            if (player_bullets[i].x >= ax && player_bullets[i].x <= ax + 9 &&
                player_bullets[i].y >= ay && player_bullets[i].y <= ay + 6) {
              invaders_alive[r][c] = false;
              player_bullets[i].active = false;
              invaders_score += (3 - r) * 10;
              break;
            }
          }
        }
        if (!player_bullets[i].active) break;
      }

      // Colisão com Bunkers
      for (int b = 0; b < 3; b++) {
        int bx = 22 + b * 38;
        if (invaders_bunkers[b] > 0 &&
            player_bullets[i].x >= bx && player_bullets[i].x <= bx + 12 &&
            player_bullets[i].y >= 44 && player_bullets[i].y <= 50) {
          invaders_bunkers[b]--;
          player_bullets[i].active = false;
          break;
        }
      }
    }
  }

  // Atualiza bombas dos aliens
  for (int i = 0; i < MAX_ALIEN_BOMBS; i++) {
    if (alien_bombs[i].active) {
      alien_bombs[i].y += 2.0f;
      if (alien_bombs[i].y > 60) {
        alien_bombs[i].active = false;
        continue;
      }

      // Colisão com Bunkers
      for (int b = 0; b < 3; b++) {
        int bx = 22 + b * 38;
        if (invaders_bunkers[b] > 0 &&
            alien_bombs[i].x >= bx && alien_bombs[i].x <= bx + 12 &&
            alien_bombs[i].y >= 44 && alien_bombs[i].y <= 50) {
          invaders_bunkers[b]--;
          alien_bombs[i].active = false;
          break;
        }
      }

      // Colisão com Jogador
      if (alien_bombs[i].active &&
          alien_bombs[i].x >= invaders_player_x && alien_bombs[i].x <= invaders_player_x + 11 &&
          alien_bombs[i].y >= 54 && alien_bombs[i].y <= 60) {
        alien_bombs[i].active = false;
        invaders_lives--;
        if (invaders_lives <= 0) {
          invaders_game_over = true;
        }
      }
    }
  }

  // Render Top Header HUD
  display.setFont(u8g2_font_5x7_tf);
  char buf[24];
  snprintf(buf, sizeof(buf), "SCORE:%04d", invaders_score);
  display.drawStr(2, 7, buf);

  // Vidas (mini canhões)
  for (int l = 0; l < invaders_lives; l++) {
    display.drawBox(100 + l * 8, 3, 5, 3);
    display.drawPixel(102 + l * 8, 2);
  }
  display.drawHLine(0, 8, 128);

  // Render Aliens
  for (int r = 0; r < INVADERS_ROWS; r++) {
    for (int c = 0; c < INVADERS_COLS; c++) {
      if (invaders_alive[r][c]) {
        int ax = (int)(invaders_fleet_x + c * 14);
        int ay = (int)(invaders_fleet_y + r * 8);
        if (invaders_anim_frame) {
          // Pose A
          display.drawBox(ax + 1, ay, 6, 4);
          display.drawPixel(ax, ay + 1);
          display.drawPixel(ax + 7, ay + 1);
          display.drawPixel(ax + 1, ay + 4);
          display.drawPixel(ax + 6, ay + 4);
        } else {
          // Pose B
          display.drawBox(ax + 1, ay + 1, 6, 4);
          display.drawPixel(ax, ay + 3);
          display.drawPixel(ax + 7, ay + 3);
          display.drawPixel(ax + 2, ay);
          display.drawPixel(ax + 5, ay);
        }
      }
    }
  }

  // Render Bunkers
  for (int b = 0; b < 3; b++) {
    if (invaders_bunkers[b] > 0) {
      int bx = 22 + b * 38;
      int bh = invaders_bunkers[b] * 2;
      display.drawRBox(bx, 48 - bh, 12, bh, 1);
    }
  }

  // Render Tiros do Jogador
  for (int i = 0; i < MAX_PLAYER_BULLETS; i++) {
    if (player_bullets[i].active) {
      display.drawVLine((int)player_bullets[i].x, (int)player_bullets[i].y, 3);
    }
  }

  // Render Bombas dos Aliens
  for (int i = 0; i < MAX_ALIEN_BOMBS; i++) {
    if (alien_bombs[i].active) {
      display.drawPixel((int)alien_bombs[i].x, (int)alien_bombs[i].y);
      display.drawPixel((int)alien_bombs[i].x, (int)alien_bombs[i].y + 2);
    }
  }

  // Render Jogador (Tanque de Defesa)
  if (!invaders_game_over || invaders_lives > 0) {
    display.drawBox(invaders_player_x + 1, 57, 9, 4);
    display.drawBox(invaders_player_x + 4, 55, 3, 2);
    display.drawPixel(invaders_player_x + 5, 54);
  }

  // Game Over / Victory Overlay
  if (invaders_game_over) {
    display.drawBox(10, 18, 108, 28);
    display.setDrawColor(0);
    display.drawFrame(12, 20, 104, 24);
    display.setFont(u8g2_font_7x14B_tf);
    if (invaders_victory) {
      display.drawStr(18, 33, "EARTH SAVED!");
    } else {
      display.drawStr(26, 33, "GAME OVER");
    }
    display.setFont(u8g2_font_5x7_tf);
    display.drawStr(14, 42, "ENT:Replay  LFT:Exit");
    display.setDrawColor(1);
  }
}

// =============================================================================
// JOGO 3: DOOM 3D (RAYCASTER ENGINE MONOCROMÁTICO COM ASSETS OFICIAIS DOOM-NANO)
// =============================================================================
#define DOOM_MAP_W 16
#define DOOM_MAP_H 16

static const uint8_t DOOM_MAP[DOOM_MAP_H][DOOM_MAP_W] = {
  {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1},
  {1,0,0,0,0,0,1,0,0,0,0,0,0,0,0,1},
  {1,0,1,1,0,0,1,0,1,1,1,0,1,1,0,1},
  {1,0,1,0,0,0,0,0,0,0,1,0,0,1,0,1},
  {1,0,1,0,1,1,0,1,1,0,1,1,0,1,0,1},
  {1,0,0,0,1,0,0,0,1,0,0,0,0,0,0,1},
  {1,1,1,0,1,0,0,0,1,0,1,1,1,1,0,1},
  {1,0,0,0,0,0,0,0,0,0,0,0,0,1,0,1},
  {1,0,1,1,1,0,1,1,1,0,1,1,0,1,0,1},
  {1,0,0,0,1,0,1,0,1,0,1,0,0,0,0,1},
  {1,1,1,0,1,0,1,0,1,0,1,0,1,1,1,1},
  {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
  {1,0,1,1,0,1,1,1,1,1,0,1,1,1,0,1},
  {1,0,1,0,0,0,0,0,0,0,0,0,0,1,0,1},
  {1,0,0,0,1,1,0,0,0,1,1,0,0,0,0,1},
  {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1}
};

static float doom_pos_x   = 2.5f;
static float doom_pos_y   = 7.5f;
static float doom_dir_x   = 1.0f;
static float doom_dir_y   = 0.0f;
static float doom_plane_x = 0.0f;
static float doom_plane_y = 0.66f;

static int   doom_hp        = 100;
static int   doom_ammo      = 25;
static int   doom_kills     = 0;
static int   doom_gun_pos   = 0;
static int   doom_flash     = 0;
static float doom_jogging   = 0.0f;
static float doom_z_buffer[64];

// Checagem de colisão com paredes do mapa
static inline bool is_doom_wall(float x, float y) {
  int mx = (int)x;
  int my = (int)y;
  if (mx < 0 || mx >= DOOM_MAP_W || my < 0 || my >= DOOM_MAP_H) return true;
  return DOOM_MAP[my][mx] > 0;
}

// Bounding box com raio de 0.20 para permitir deslizamento suave pelas paredes
static inline bool can_player_stand(float x, float y) {
  const float r = 0.20f;
  if (is_doom_wall(x - r, y - r)) return false;
  if (is_doom_wall(x + r, y - r)) return false;
  if (is_doom_wall(x - r, y + r)) return false;
  if (is_doom_wall(x + r, y + r)) return false;
  return true;
}

// Entidades 3D DOOM: 0 = Imp, 1 = Fireball, 2 = Medikit
struct DoomEntity {
  float x, y;
  float vx, vy;
  uint8_t type;    // 0 = Imp, 1 = Fireball, 2 = Medikit
  uint8_t state;   // 0 = walk1, 1 = walk2, 2 = attack, 3 = hit, 4 = dead, 5 = picked
  int   hp;
  uint32_t timer;
  bool  active;
  float dist;
};

#define DOOM_MAX_ENTITIES 8
static DoomEntity doom_entities[DOOM_MAX_ENTITIES];
static bool doom_in_intro = true;

void init_doom() {
  doom_pos_x   = 2.5f;
  doom_pos_y   = 7.5f;
  doom_dir_x   = 1.0f;
  doom_dir_y   = 0.0f;
  doom_plane_x = 0.0f;
  doom_plane_y = 0.66f;
  doom_hp      = 100;
  doom_ammo    = 25;
  doom_kills   = 0;
  doom_gun_pos = 0;
  doom_flash   = 0;
  doom_jogging = 0.0f;
  doom_in_intro = true;

  for (int i = 0; i < DOOM_MAX_ENTITIES; i++) {
    doom_entities[i].active = false;
  }

  // 3 Imps posicionados em posições válidas e abertas do mapa
  doom_entities[0] = { 5.5f, 5.5f, 0, 0, 0, 0, 50, 0, true, 0 };
  doom_entities[1] = { 8.5f, 3.5f, 0, 0, 0, 0, 50, 0, true, 0 };
  doom_entities[2] = { 12.5f, 11.5f, 0, 0, 0, 0, 50, 0, true, 0 };

  // 1 Medikit (+40 HP) no grande corredor
  doom_entities[3] = { 8.5f, 7.5f, 0, 0, 2, 0, 0, 0, true, 0 };
}

// Renderizador com escala e transparência de sprites DOOM
static void render_doom_sprite(
  float world_x, float world_y,
  const uint8_t *bitmap, const uint8_t *mask,
  int16_t orig_w, int16_t orig_h,
  uint8_t frame_idx,
  float view_height
) {
  float sprite_x = world_x - doom_pos_x;
  float sprite_y = world_y - doom_pos_y;

  float inv_det = 1.0f / (doom_plane_x * doom_dir_y - doom_dir_x * doom_plane_y);
  float transform_x = inv_det * (doom_dir_y * sprite_x - doom_dir_x * sprite_y);
  float transform_y = inv_det * (-doom_plane_y * sprite_x + doom_plane_x * sprite_y);

  if (transform_y <= 0.35f || transform_y > 9.0f) return;

  int screen_cx = (int)(64.0f * (1.0f + transform_x / transform_y));
  float scale = 1.0f / transform_y;
  int tw = (int)(orig_w * scale);
  int th = (int)(orig_h * scale);
  if (tw < 2 || th < 2) return;

  int start_x = screen_cx - tw / 2;
  int start_y = (int)(27.0f - th / 2.0f + (view_height * scale));

  if (start_x + tw < 0 || start_x >= 128) return;
  if (start_y + th < 0 || start_y >= 54) return;

  uint16_t byte_width = orig_w / 8;
  uint16_t frame_offset = byte_width * orig_h * frame_idx;

  float step_x = (float)orig_w / (float)tw;
  float step_y = (float)orig_h / (float)th;

  for (int py = 0; py < th; py++) {
    int sy = start_y + py;
    if (sy < 0 || sy >= 54) continue;
    int src_y = (int)(py * step_y);
    if (src_y >= orig_h) src_y = orig_h - 1;

    for (int px = 0; px < tw; px++) {
      int sx = start_x + px;
      if (sx < 0 || sx >= 128) continue;

      int z_col = sx / 2;
      if (z_col >= 0 && z_col < 64) {
        if (doom_z_buffer[z_col] < transform_y) continue;
      }

      int src_x = (int)(px * step_x);
      if (src_x >= orig_w) src_x = orig_w - 1;

      uint16_t boff = frame_offset + src_y * byte_width + (src_x / 8);
      uint8_t bbit = src_x % 8;

      if (mask[boff] & doom_bit_mask[bbit]) {
        bool bit = (bitmap[boff] & doom_bit_mask[bbit]) != 0;
        if (bit) {
          display.drawPixel(sx, sy);
        } else {
          display.setDrawColor(0);
          display.drawPixel(sx, sy);
          display.setDrawColor(1);
        }
      }
    }
  }
}

// Renderizador da espingarda e muzzle flash autênticos
static void render_doom_weapon(int gun_pos, float jogging) {
  int x = 48 + (int)(std::sin((float)millis() * 0.007f) * 8.0f * jogging);
  int y = 54 - 32 + gun_pos + (int)(fabsf(std::cos((float)millis() * 0.007f)) * 4.0f * jogging);

  // Muzzle Flash no momento do tiro
  if (gun_pos > 4) {
    for (int fy = 0; fy < BMP_FIRE_HEIGHT; fy++) {
      int sy = y - 10 + fy;
      if (sy < 0 || sy >= 54) continue;
      for (int fx = 0; fx < BMP_FIRE_WIDTH; fx++) {
        int sx = x + 4 + fx;
        if (sx < 0 || sx >= 128) continue;
        uint16_t boff = fy * 3 + (fx / 8);
        if (bmp_fire_bits[boff] & doom_bit_mask[fx % 8]) {
          display.drawPixel(sx, sy);
        }
      }
    }
  }

  // Shotgun oficial de cano duplo (32x32)
  for (int gy = 0; gy < BMP_GUN_HEIGHT; gy++) {
    int sy = y + gy;
    if (sy < 0 || sy >= 54) continue;
    for (int gx = 0; gx < BMP_GUN_WIDTH; gx++) {
      int sx = x + gx;
      if (sx < 0 || sx >= 128) continue;
      uint16_t boff = gy * 4 + (gx / 8);
      if (bmp_gun_mask[boff] & doom_bit_mask[gx % 8]) {
        bool bit = (bmp_gun_bits[boff] & doom_bit_mask[gx % 8]) != 0;
        display.setDrawColor(bit ? 1 : 0);
        display.drawPixel(sx, sy);
      }
    }
  }
  display.setDrawColor(1);
}

// Pixel de gradiente de iluminação 1-bit
static inline bool get_doom_gradient_pixel(uint8_t x, uint8_t y, uint8_t intensity) {
  if (intensity == 0) return false;
  if (intensity >= 7) return true;
  uint8_t idx = intensity * 16 + (y % 8) * 2 + (x / 8) % 2;
  return (gradient[idx] & doom_bit_mask[x % 8]) != 0;
}

void render_game_doom() {
  if (doom_in_intro) {
    display.drawBitmap(28, 4, 9, 47, bmp_logo_bits);
    display.setFont(u8g2_font_5x7_tf);
    if ((millis() / 450) % 2 == 0) {
      display.drawStr(30, 60, "[ PRESS ENTER ]");
    }
    return;
  }

  // Flash de dano recebido (inverte tela temporariamente)
  if (doom_flash > 0) {
    display.drawBox(0, 0, 128, 54);
    doom_flash--;
    return;
  }

  // Recoil da arma retornando à posição de repouso
  if (doom_gun_pos > 0) doom_gun_pos -= 2;

  // Head bobbing ao caminhar
  float view_height = fabsf(std::sin((float)millis() * 0.007f)) * 4.0f * doom_jogging;

  // Linha do horizonte
  display.drawHLine(0, 27, 128);

  // Raycasting Loop (64 colunas de 2 pixels de largura)
  for (int col = 0; col < 64; col++) {
    int screen_x = col * 2;
    float camera_x = 2.0f * screen_x / 128.0f - 1.0f;
    float ray_dir_x = doom_dir_x + doom_plane_x * camera_x;
    float ray_dir_y = doom_dir_y + doom_plane_y * camera_x;

    int map_x = (int)doom_pos_x;
    int map_y = (int)doom_pos_y;

    float delta_dist_x = (ray_dir_x == 0) ? 1e30f : fabsf(1.0f / ray_dir_x);
    float delta_dist_y = (ray_dir_y == 0) ? 1e30f : fabsf(1.0f / ray_dir_y);
    float side_dist_x, side_dist_y;
    int step_x, step_y;
    int hit = 0, side = 0;

    if (ray_dir_x < 0) {
      step_x = -1;
      side_dist_x = (doom_pos_x - map_x) * delta_dist_x;
    } else {
      step_x = 1;
      side_dist_x = (map_x + 1.0f - doom_pos_x) * delta_dist_x;
    }
    if (ray_dir_y < 0) {
      step_y = -1;
      side_dist_y = (doom_pos_y - map_y) * delta_dist_y;
    } else {
      step_y = 1;
      side_dist_y = (map_y + 1.0f - doom_pos_y) * delta_dist_y;
    }

    while (hit == 0) {
      if (side_dist_x < side_dist_y) {
        side_dist_x += delta_dist_x;
        map_x += step_x;
        side = 0;
      } else {
        side_dist_y += delta_dist_y;
        map_y += step_y;
        side = 1;
      }
      if (map_x >= 0 && map_x < DOOM_MAP_W && map_y >= 0 && map_y < DOOM_MAP_H) {
        if (DOOM_MAP[map_y][map_x] > 0) hit = 1;
      } else {
        hit = 1;
      }
    }

    float perp_wall_dist = (side == 0) ? (side_dist_x - delta_dist_x) : (side_dist_y - delta_dist_y);
    if (perp_wall_dist < 0.1f) perp_wall_dist = 0.1f;
    doom_z_buffer[col] = perp_wall_dist;

    int line_height = (int)(46.0f / perp_wall_dist);
    int draw_start = -line_height / 2 + 27 + (int)(view_height / perp_wall_dist);
    if (draw_start < 0) draw_start = 0;
    int draw_end = line_height / 2 + 27 + (int)(view_height / perp_wall_dist);
    if (draw_end >= 54) draw_end = 53;

    // Gradiente de iluminação 1-bit autêntico do Doom-Nano
    int intensity = 7 - (int)(perp_wall_dist * 0.85f) - (side * 2);
    if (intensity < 0) intensity = 0;
    if (intensity > 7) intensity = 7;

    if (intensity >= 7) {
      display.drawBox(screen_x, draw_start, 2, draw_end - draw_start + 1);
    } else if (intensity > 0) {
      for (int y = draw_start; y <= draw_end; y++) {
        if (get_doom_gradient_pixel(screen_x, y, intensity)) {
          display.drawPixel(screen_x, y);
          display.drawPixel(screen_x + 1, y);
        }
      }
    }
  }

  // Atualização da IA dos Imps, Fireballs e Coletáveis
  for (int i = 0; i < DOOM_MAX_ENTITIES; i++) {
    if (!doom_entities[i].active) continue;

    if (doom_entities[i].type == 0) {
      // Imp Demon
      float dx = doom_pos_x - doom_entities[i].x;
      float dy = doom_pos_y - doom_entities[i].y;
      float dist = std::sqrt(dx * dx + dy * dy);
      doom_entities[i].dist = dist;

      if (doom_entities[i].state == 4) {
        // Morto: permanece no chão
        continue;
      }

      if (doom_entities[i].state == 3) {
        // Recuperando de dor do tiro
        if (millis() - doom_entities[i].timer > 300) {
          doom_entities[i].state = 0;
        }
      } else if (doom_hp > 0 && dist < 9.0f) {
        if (dist > 1.2f) {
          // Anda em direção ao jogador
          float speed = 0.02f;
          float dx_step = (dx / dist) * speed;
          float dy_step = (dy / dist) * speed;
          if (can_player_stand(doom_entities[i].x + dx_step, doom_entities[i].y)) doom_entities[i].x += dx_step;
          if (can_player_stand(doom_entities[i].x, doom_entities[i].y + dy_step)) doom_entities[i].y += dy_step;

          // Alterna pernas ao caminhar
          doom_entities[i].state = ((millis() / 350) % 2 == 0) ? 0 : 1;

          // Dispara Fireball se estiver à média distância
          if (dist > 2.5f && dist < 7.0f && millis() - doom_entities[i].timer > 3500) {
            doom_entities[i].timer = millis();
            doom_entities[i].state = 2; // Frame de ataque
            for (int f = 4; f < DOOM_MAX_ENTITIES; f++) {
              if (!doom_entities[f].active) {
                float f_speed = 0.08f;
                doom_entities[f].x = doom_entities[i].x;
                doom_entities[f].y = doom_entities[i].y;
                doom_entities[f].vx = (dx / dist) * f_speed;
                doom_entities[f].vy = (dy / dist) * f_speed;
                doom_entities[f].type = 1;
                doom_entities[f].state = 0;
                doom_entities[f].hp = 1;
                doom_entities[f].timer = millis();
                doom_entities[f].active = true;
                break;
              }
            }
          }
        } else {
          // Ataque corpo a corpo do Imp
          doom_entities[i].state = 2;
          static uint32_t last_melee = 0;
          if (millis() - last_melee > 800) {
            last_melee = millis();
            doom_hp -= 12;
            if (doom_hp < 0) doom_hp = 0;
            doom_flash = 2;
          }
        }
      }
    } else if (doom_entities[i].type == 1) {
      // Fireball Projétil
      doom_entities[i].x += doom_entities[i].vx;
      doom_entities[i].y += doom_entities[i].vy;
      float f_dx = doom_pos_x - doom_entities[i].x;
      float f_dy = doom_pos_y - doom_entities[i].y;
      float f_dist = std::sqrt(f_dx * f_dx + f_dy * f_dy);
      doom_entities[i].dist = f_dist;

      if (DOOM_MAP[(int)doom_entities[i].y][(int)doom_entities[i].x] != 0 || millis() - doom_entities[i].timer > 4000) {
        doom_entities[i].active = false;
      } else if (f_dist < 0.65f && doom_hp > 0) {
        doom_hp -= 15;
        if (doom_hp < 0) doom_hp = 0;
        doom_flash = 2;
        doom_entities[i].active = false;
      }
    } else if (doom_entities[i].type == 2) {
      // Medikit
      float m_dx = doom_pos_x - doom_entities[i].x;
      float m_dy = doom_pos_y - doom_entities[i].y;
      float m_dist = std::sqrt(m_dx * m_dx + m_dy * m_dy);
      doom_entities[i].dist = m_dist;
      if (m_dist < 0.8f && doom_hp > 0) {
        doom_hp += 40;
        if (doom_hp > 100) doom_hp = 100;
        doom_entities[i].active = false;
      }
    }
  }

  // Ordenação das entidades pelo algoritmo do pintor (distância decrescente)
  int sorted_indices[DOOM_MAX_ENTITIES];
  int count = 0;
  for (int i = 0; i < DOOM_MAX_ENTITIES; i++) {
    if (doom_entities[i].active) {
      sorted_indices[count++] = i;
    }
  }
  for (int i = 0; i < count - 1; i++) {
    for (int j = i + 1; j < count; j++) {
      if (doom_entities[sorted_indices[i]].dist < doom_entities[sorted_indices[j]].dist) {
        int tmp = sorted_indices[i];
        sorted_indices[i] = sorted_indices[j];
        sorted_indices[j] = tmp;
      }
    }
  }
  for (int s = 0; s < count; s++) {
    int idx = sorted_indices[s];
    if (doom_entities[idx].type == 0) {
      // Imp Demon com chifres, animação de andar, ataque, dor ou morte
      render_doom_sprite(doom_entities[idx].x, doom_entities[idx].y, bmp_imp_bits, bmp_imp_mask, BMP_IMP_WIDTH, BMP_IMP_HEIGHT, doom_entities[idx].state, view_height);
    } else if (doom_entities[idx].type == 1) {
      // Fireball voando
      render_doom_sprite(doom_entities[idx].x, doom_entities[idx].y, bmp_fireball_bits, bmp_fireball_mask, BMP_FIREBALL_WIDTH, BMP_FIREBALL_HEIGHT, 0, view_height);
    } else if (doom_entities[idx].type == 2) {
      // Medikit
      render_doom_sprite(doom_entities[idx].x, doom_entities[idx].y, bmp_items_bits, bmp_items_mask, BMP_ITEMS_WIDTH, BMP_ITEMS_HEIGHT, 0, view_height);
    }
  }

  // Mira Central
  display.drawPixel(64, 27);
  display.drawHLine(61, 27, 2);
  display.drawHLine(66, 27, 2);
  display.drawVLine(64, 24, 2);
  display.drawVLine(64, 29, 2);

  // Render Espingarda de Cano Duplo (Double Barrel Shotgun) autêntica
  render_doom_weapon(doom_gun_pos, doom_jogging);

  // Status Bar / HUD Inferior (Y=54..63)
  display.drawBox(0, 54, 128, 10);
  display.setDrawColor(0);
  display.setFont(u8g2_font_5x7_tf);
  char hud_buf[32];
  snprintf(hud_buf, sizeof(hud_buf), "HP:%d%%  AMMO:%d  KIL:%d/3", doom_hp, doom_ammo, doom_kills);
  display.drawStr(2, 62, hud_buf);
  display.setDrawColor(1);

  // Condições de Fim de Jogo
  if (doom_hp <= 0) {
    display.drawBox(10, 18, 108, 28);
    display.setDrawColor(0);
    display.drawFrame(12, 20, 104, 24);
    display.setFont(u8g2_font_7x14B_tf);
    display.drawStr(34, 33, "YOU DIED");
    display.setFont(u8g2_font_5x7_tf);
    display.drawStr(14, 42, "ENT:Replay  LFT:Exit");
    display.setDrawColor(1);
  } else if (doom_kills >= 3) {
    display.drawBox(10, 18, 108, 28);
    display.setDrawColor(0);
    display.drawFrame(12, 20, 104, 24);
    display.setFont(u8g2_font_7x14B_tf);
    display.drawStr(18, 33, "HELL PURGED!");
    display.setFont(u8g2_font_5x7_tf);
    display.drawStr(14, 42, "ENT:Replay  LFT:Exit");
    display.setDrawColor(1);
  }
}

// =============================================================================
// JOGO 4: SNAKE ARCADE
// =============================================================================
#define SNAKE_GRID_W 26
#define SNAKE_GRID_H 15
#define MAX_SNAKE_LEN 100

static int8_t snake_body_x[MAX_SNAKE_LEN];
static int8_t snake_body_y[MAX_SNAKE_LEN];
static int    snake_len = 4;
static int8_t snake_dir_x = 1;
static int8_t snake_dir_y = 0;
static int8_t snake_next_dir_x = 1;
static int8_t snake_next_dir_y = 0;
static int8_t snake_food_x = 12;
static int8_t snake_food_y = 7;
static int    snake_score = 0;
static int    snake_best  = 0;
static bool   snake_game_over = false;
static uint32_t snake_last_tick = 0;

static void snake_spawn_food() {
  bool valid = false;
  while (!valid) {
    snake_food_x = rand() % SNAKE_GRID_W;
    snake_food_y = rand() % SNAKE_GRID_H;
    valid = true;
    for (int i = 0; i < snake_len; i++) {
      if (snake_body_x[i] == snake_food_x && snake_body_y[i] == snake_food_y) {
        valid = false;
        break;
      }
    }
  }
}

void init_snake() {
  snake_len = 4;
  snake_dir_x = 1;
  snake_dir_y = 0;
  snake_next_dir_x = 1;
  snake_next_dir_y = 0;
  snake_score = 0;
  snake_game_over = false;

  for (int i = 0; i < snake_len; i++) {
    snake_body_x[i] = 8 - i;
    snake_body_y[i] = 7;
  }
  snake_spawn_food();
  snake_last_tick = millis();
}

void render_game_snake() {
  uint32_t now = millis();

  // Tick da cobra
  uint32_t speed = 140 - (snake_score * 3);
  if (speed < 60) speed = 60;

  if (!snake_game_over && now - snake_last_tick > speed) {
    snake_dir_x = snake_next_dir_x;
    snake_dir_y = snake_next_dir_y;

    int next_x = snake_body_x[0] + snake_dir_x;
    int next_y = snake_body_y[0] + snake_dir_y;

    // Colisão com parede
    if (next_x < 0 || next_x >= SNAKE_GRID_W || next_y < 0 || next_y >= SNAKE_GRID_H) {
      snake_game_over = true;
    }

    // Colisão com próprio corpo
    for (int i = 0; i < snake_len; i++) {
      if (snake_body_x[i] == next_x && snake_body_y[i] == next_y) {
        snake_game_over = true;
      }
    }

    if (!snake_game_over) {
      // Comeu maçã?
      if (next_x == snake_food_x && next_y == snake_food_y) {
        if (snake_len < MAX_SNAKE_LEN) snake_len++;
        snake_score += 10;
        if (snake_score > snake_best) snake_best = snake_score;
        snake_spawn_food();
      }

      // Desloca corpo
      for (int i = snake_len - 1; i > 0; i--) {
        snake_body_x[i] = snake_body_x[i - 1];
        snake_body_y[i] = snake_body_y[i - 1];
      }
      snake_body_x[0] = next_x;
      snake_body_y[0] = next_y;
    }
    snake_last_tick = now;
  }

  // Grade de jogo (X=3, Y=4, W=80, H=56)
  display.drawFrame(3, 4, 80, 56);

  // Render Maçã
  display.drawBox(4 + snake_food_x * 3, 5 + snake_food_y * 3 + 1, 2, 2);
  display.drawPixel(5 + snake_food_x * 3, 5 + snake_food_y * 3);

  // Render Cobra
  for (int i = 0; i < snake_len; i++) {
    int bx = 4 + snake_body_x[i] * 3;
    int by = 5 + snake_body_y[i] * 3;
    if (i == 0) {
      // Cabeça
      display.drawRBox(bx, by, 3, 3, 1);
    } else {
      display.drawBox(bx, by, 2, 2);
    }
  }

  // Painel Lateral (X=86..126)
  display.setFont(u8g2_font_6x10_tf);
  display.drawStr(88, 14, "SNAKE");
  display.setFont(u8g2_font_5x7_tf);
  display.drawStr(88, 26, "SCORE");
  char buf[16];
  snprintf(buf, sizeof(buf), "%d", snake_score);
  display.drawStr(88, 35, buf);
  display.drawStr(88, 46, "BEST");
  snprintf(buf, sizeof(buf), "%d", snake_best);
  display.drawStr(88, 55, buf);

  // Game Over
  if (snake_game_over) {
    display.drawBox(10, 18, 108, 28);
    display.setDrawColor(0);
    display.drawFrame(12, 20, 104, 24);
    display.setFont(u8g2_font_7x14B_tf);
    display.drawStr(26, 33, "GAME OVER");
    display.setFont(u8g2_font_5x7_tf);
    display.drawStr(14, 42, "ENT:Replay  LFT:Exit");
    display.setDrawColor(1);
  }
}

// =============================================================================
// ENTRADAS E CONTROLES ESPECÍFICOS DE CADA JOGO
// =============================================================================
void handle_tetris_input(bool up, bool dn, bool left, bool right, bool enter) {
  if (tetris_game_over) {
    if (enter) init_tetris();
    if (left)  { current_screen = SCR_GAMES_MENU; menu_sel = 0; }
    return;
  }
  if (left) {
    if (!tetris_collides(tetris_px - 1, tetris_py, tetris_shape, tetris_rot)) {
      tetris_px--;
    }
  }
  if (right) {
    if (!tetris_collides(tetris_px + 1, tetris_py, tetris_shape, tetris_rot)) {
      tetris_px++;
    }
  }
  // BOTÃO DO MEIO (ENTER): GIRA A PEÇA (Rotate piece)
  if (enter) {
    int next_rot = (tetris_rot + 1) % 4;
    if (!tetris_collides(tetris_px, tetris_py, tetris_shape, next_rot)) {
      tetris_rot = next_rot;
    }
  }
  // BOTÃO CIMA (UP): Hard drop imediato (ou girar secundário)
  if (up) {
    while (!tetris_collides(tetris_px, tetris_py + 1, tetris_shape, tetris_rot)) {
      tetris_py++;
    }
    tetris_lock_piece();
  }
  // BOTÃO BAIXO (DN): Descer rápido (soft drop)
  if (dn) {
    if (!tetris_collides(tetris_px, tetris_py + 1, tetris_shape, tetris_rot)) {
      tetris_py++;
    }
  }
}

void handle_invaders_input(bool up, bool dn, bool left, bool right, bool enter) {
  if (invaders_game_over) {
    if (enter) init_invaders();
    if (left)  { current_screen = SCR_GAMES_MENU; menu_sel = 1; }
    return;
  }
  if (left) {
    if (invaders_player_x > 4) invaders_player_x -= 3;
  }
  if (right) {
    if (invaders_player_x < 114) invaders_player_x += 3;
  }
  if (enter || up) {
    // Dispara tiro
    for (int i = 0; i < MAX_PLAYER_BULLETS; i++) {
      if (!player_bullets[i].active) {
        player_bullets[i].x = invaders_player_x + 5;
        player_bullets[i].y = 53;
        player_bullets[i].active = true;
        break;
      }
    }
  }
  if (dn) {
    // Pausa / Retorno ao menu
    current_screen = SCR_GAMES_MENU;
    menu_sel = 1;
  }
}

void handle_doom_input(bool up, bool dn, bool left, bool right, bool enter) {
  (void)up; (void)dn; (void)right;
  if (doom_in_intro) {
    if (enter) doom_in_intro = false;
    if (left)  { current_screen = SCR_GAMES_MENU; menu_sel = 2; }
    return;
  }
  if (doom_hp <= 0 || doom_kills >= 3) {
    if (enter) init_doom();
    if (left)  { current_screen = SCR_GAMES_MENU; menu_sel = 2; }
    return;
  }

  // Limitação de taxa de atualização de física (30ms = ~33 FPS constantes)
  // Sem isso, em processadores rápidos (200MHz), o loop roda centenas de vezes por segundo
  // fazendo o jogador "se teletransportar"
  static uint32_t doom_last_move_ms = 0;
  uint32_t now = millis();
  uint32_t dt = now - doom_last_move_ms;
  if (dt < 25) {
    return; // Aguarda o intervalo de frame
  }
  doom_last_move_ms = now;
  if (dt > 100) dt = 100;

  float dt_factor = (float)dt / 30.0f;
  float move_speed = 0.07f * dt_factor;
  float rot_speed  = 0.055f * dt_factor;

  if (digitalRead(BTN_LEFT) == LOW || left) {
    float old_dir_x = doom_dir_x;
    doom_dir_x = doom_dir_x * std::cos(-rot_speed) - doom_dir_y * std::sin(-rot_speed);
    doom_dir_y = old_dir_x * std::sin(-rot_speed) + doom_dir_y * std::cos(-rot_speed);
    float old_plane_x = doom_plane_x;
    doom_plane_x = doom_plane_x * std::cos(-rot_speed) - doom_plane_y * std::sin(-rot_speed);
    doom_plane_y = old_plane_x * std::sin(-rot_speed) + doom_plane_y * std::cos(-rot_speed);
  }
  if (digitalRead(BTN_RIGHT) == LOW || right) {
    float old_dir_x = doom_dir_x;
    doom_dir_x = doom_dir_x * std::cos(rot_speed) - doom_dir_y * std::sin(rot_speed);
    doom_dir_y = old_dir_x * std::sin(rot_speed) + doom_dir_y * std::cos(rot_speed);
    float old_plane_x = doom_plane_x;
    doom_plane_x = doom_plane_x * std::cos(rot_speed) - doom_plane_y * std::sin(rot_speed);
    doom_plane_y = old_plane_x * std::sin(rot_speed) + doom_plane_y * std::cos(rot_speed);
  }

  bool is_moving = false;
  if (digitalRead(BTN_UP) == LOW || up) {
    float dx = doom_dir_x * move_speed;
    float dy = doom_dir_y * move_speed;
    if (can_player_stand(doom_pos_x + dx, doom_pos_y)) doom_pos_x += dx;
    if (can_player_stand(doom_pos_x, doom_pos_y + dy)) doom_pos_y += dy;
    is_moving = true;
    doom_jogging = 1.0f;
  } else if (digitalRead(BTN_DOWN) == LOW || dn) {
    float dx = -doom_dir_x * move_speed;
    float dy = -doom_dir_y * move_speed;
    if (can_player_stand(doom_pos_x + dx, doom_pos_y)) doom_pos_x += dx;
    if (can_player_stand(doom_pos_x, doom_pos_y + dy)) doom_pos_y += dy;
    is_moving = true;
    doom_jogging = 0.8f;
  }

  if (!is_moving) {
    doom_jogging *= 0.6f;
  }

  // DISPARAR SHOTGUN (ENTER)
  if (enter && doom_ammo > 0) {
    doom_ammo--;
    doom_gun_pos = 14; // Recoil da shotgun e muzzle flash

    // Hitscan no centro da mira contra Imps
    int hit_idx = -1;
    float closest_dist = 999.0f;

    for (int i = 0; i < DOOM_MAX_ENTITIES; i++) {
      if (!doom_entities[i].active || doom_entities[i].type != 0 || doom_entities[i].state == 4) continue;
      float sprite_x = doom_entities[i].x - doom_pos_x;
      float sprite_y = doom_entities[i].y - doom_pos_y;
      float inv_det = 1.0f / (doom_plane_x * doom_dir_y - doom_dir_x * doom_plane_y);
      float transform_x = inv_det * (doom_dir_y * sprite_x - doom_dir_x * sprite_y);
      float transform_y = inv_det * (-doom_plane_y * sprite_x + doom_plane_x * sprite_y);

      if (transform_y > 0.4f && transform_y < closest_dist && transform_y < 8.0f) {
        if (fabsf(transform_x / transform_y) < 0.28f) {
          closest_dist = transform_y;
          hit_idx = i;
        }
      }
    }

    if (hit_idx >= 0) {
      // Tiro certeiro no Imp!
      doom_entities[hit_idx].hp -= 50;
      if (doom_entities[hit_idx].hp <= 0) {
        doom_entities[hit_idx].state = 4; // Imp cai morto no chão
        doom_kills++;
      } else {
        doom_entities[hit_idx].state = 3; // Imp reage à dor
        doom_entities[hit_idx].timer = millis();
      }
    }
  }
}

void handle_snake_input(bool up, bool dn, bool left, bool right, bool enter) {
  if (snake_game_over) {
    if (enter) init_snake();
    if (left)  { current_screen = SCR_GAMES_MENU; menu_sel = 3; }
    return;
  }
  if (up && snake_dir_y == 0) {
    snake_next_dir_x = 0;
    snake_next_dir_y = -1;
  } else if (dn && snake_dir_y == 0) {
    snake_next_dir_x = 0;
    snake_next_dir_y = 1;
  } else if (left && snake_dir_x == 0) {
    snake_next_dir_x = -1;
    snake_next_dir_y = 0;
  } else if (right && snake_dir_x == 0) {
    snake_next_dir_x = 1;
    snake_next_dir_y = 0;
  }
}
