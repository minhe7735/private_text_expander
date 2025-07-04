#ifndef ZMK_EXPANSION_ENGINE_H
#define ZMK_EXPANSION_ENGINE_H

#include <zephyr/kernel.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// Forward declaration
struct expansion_work;

/*
 * Defines an interface for OS-specific typing behaviors, primarily for Unicode.
 */
struct os_typing_driver {
    void (*start_unicode_typing)(struct expansion_work *exp_work);
};

enum expansion_state {
  EXPANSION_STATE_IDLE,
  EXPANSION_STATE_START_BACKSPACE,
  EXPANSION_STATE_BACKSPACE_PRESS,
  EXPANSION_STATE_BACKSPACE_RELEASE,
  EXPANSION_STATE_START_TYPING,
  EXPANSION_STATE_TYPE_CHAR_START,
  EXPANSION_STATE_TYPE_CHAR_KEY_PRESS,
  EXPANSION_STATE_TYPE_CHAR_KEY_RELEASE,
  EXPANSION_STATE_TYPE_LITERAL_CHAR,
  EXPANSION_STATE_FINISH,
  EXPANSION_STATE_REPLAY_KEY_PRESS,
  EXPANSION_STATE_REPLAY_KEY_RELEASE,

  // Unicode Start
  EXPANSION_STATE_UNICODE_START,

  // Windows Unicode States
  EXPANSION_STATE_WIN_UNI_PRESS_ALT,
  EXPANSION_STATE_WIN_UNI_TYPE_NUMPAD_PRESS,
  EXPANSION_STATE_WIN_UNI_TYPE_NUMPAD_RELEASE,
  EXPANSION_STATE_WIN_UNI_RELEASE_ALT,

  // macOS Unicode States
  EXPANSION_STATE_MAC_UNI_PRESS_OPTION,
  EXPANSION_STATE_MAC_UNI_TYPE_HEX_PRESS,
  EXPANSION_STATE_MAC_UNI_TYPE_HEX_RELEASE,
  EXPANSION_STATE_MAC_UNI_RELEASE_OPTION,

  // Linux Unicode States
  EXPANSION_STATE_LINUX_UNI_PRESS_CTRL_SHIFT,
  EXPANSION_STATE_LINUX_UNI_PRESS_U,
  EXPANSION_STATE_LINUX_UNI_RELEASE_U,
  EXPANSION_STATE_LINUX_UNI_RELEASE_CTRL_SHIFT,
  EXPANSION_STATE_LINUX_UNI_TYPE_HEX_PRESS,
  EXPANSION_STATE_LINUX_UNI_TYPE_HEX_RELEASE,
  EXPANSION_STATE_LINUX_UNI_PRESS_TERMINATOR,
  EXPANSION_STATE_LINUX_UNI_RELEASE_TERMINATOR,
};

struct expansion_work {
  struct k_work_delayable work;
  const char *expanded_text;
  uint8_t backspace_count;
  size_t text_index;
  int64_t start_time_ms;
  volatile enum expansion_state state;
  uint16_t current_keycode;
  bool current_char_needs_shift;
  bool shift_mod_active;
  uint16_t trigger_keycode_to_replay;

  size_t literal_end_index;
  uint32_t unicode_codepoint;
  char unicode_hex_buffer[9];
  uint8_t unicode_hex_index;
};

void expansion_work_handler(struct k_work *work);
int start_expansion(struct expansion_work *work_item, const char *expanded_text, uint8_t len_to_delete, uint16_t trigger_keycode);
void cancel_current_expansion(struct expansion_work *work_item);

#endif /* ZMK_EXPANSION_ENGINE_H */
