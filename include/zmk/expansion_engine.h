#ifndef ZMK_EXPANSION_ENGINE_H
#define ZMK_EXPANSION_ENGINE_H

#include <zephyr/kernel.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

enum expansion_state {
  EXPANSION_STATE_IDLE,
  EXPANSION_STATE_START_BACKSPACE,
  EXPANSION_STATE_BACKSPACE_PRESS,
  EXPANSION_STATE_BACKSPACE_RELEASE,
  EXPANSION_STATE_START_TYPING,
  EXPANSION_STATE_TYPE_CHAR_START,
  EXPANSION_STATE_TYPE_CHAR_SHIFT_PRESS,
  EXPANSION_STATE_TYPE_CHAR_KEY_PRESS,
  EXPANSION_STATE_TYPE_CHAR_KEY_RELEASE,
  EXPANSION_STATE_TYPE_CHAR_SHIFT_RELEASE,
  EXPANSION_STATE_FINISH,
};

struct expansion_work {
  struct k_work_delayable work;
  char expanded_text[CONFIG_ZMK_TEXT_EXPANDER_MAX_EXPANDED_LEN];
  uint8_t backspace_count;
  size_t text_index;
  int64_t start_time_ms;
  enum expansion_state state;
  uint32_t current_keycode;
  bool current_char_needs_shift;
};

void expansion_work_handler(struct k_work *work);
int start_expansion(const char *short_code, const char *expanded_text, uint8_t short_len);
void cancel_current_expansion(void);
struct expansion_work *get_expansion_work_item(void);

#endif
