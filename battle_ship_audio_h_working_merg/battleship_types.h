#pragma once
#include <stdint.h>

enum Player : uint8_t {
  PLAYER_1 = 0,
  PLAYER_2 = 1
};

enum GameState : uint8_t {
  WAITING_FOR_AIM,
  AIMING,
  WAITING_FOR_CONFIRM,
  GAME_OVER
};
