#pragma once
#define GRID_WIDTH 10
#define GRID_HEIGHT 20
#define N_SHAPES 7

typedef enum {
  QUIT = -1,
  NO_INPUT,
  ANY_INPUT,
  LEFT,
  RIGHT,
  DOWN,
  ROTATE,
  SOFT_DROP,
  HARD_DROP,
  START,
} input_event_t;

typedef struct {
  // Cardtunes: keep engine bookkeeping in the saveable game, not process globals.
  int rows_to_destroy;
  unsigned int lines_cleared;
  int shape_bag[N_SHAPES];
  int shape_bag_idx;
  int next_shape_kind;
  unsigned int score;
  unsigned int level;

  // Grid is represented as m x n int matrix
  // 0 denotes an empty block
  // Occupied blocks are represented by their color indices
  int grid[GRID_HEIGHT][GRID_WIDTH];

  // Array of blocks in the current shape
  // Each value pair corresponds to the shift from the shape
  // position over x and y axis
  int current_shape[8];

  // The color index of the current shape
  int current_shape_kind;

  // Current shape coordinates
  int current_x;
  int current_y;

  unsigned int fall_elapsed_ms;
  unsigned int fall_period_ms;

  unsigned int game_over;
  unsigned int changed;
} game_state_t;

game_state_t game_state_new();
void game_shape(int kind, int output[8]);
void game_state_update(game_state_t *state, input_event_t event,
                       unsigned int elapsed_ms);
