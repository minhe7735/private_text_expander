#ifndef ZMK_EXPANSION_ENGINE_H
#define ZMK_EXPANSION_ENGINE_H

#include <zephyr/kernel.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/**
 * @brief Defines the states of the expansion engine's state machine.
 */
enum expansion_state {
  EXPANSION_STATE_IDLE,
  EXPANSION_STATE_START_BACKSPACE,
  EXPANSION_STATE_BACKSPACE_PRESS,
  EXPANSION_STATE_BACKSPACE_RELEASE,
  EXPANSION_STATE_START_TYPING,
  EXPANSION_STATE_TYPE_CHAR_START,
  EXPANSION_STATE_TYPE_CHAR_KEY_PRESS,
  EXPANSION_STATE_TYPE_CHAR_KEY_RELEASE,
  EXPANSION_STATE_FINISH,
};

/**
 * @brief Data structure holding all information for an active expansion process.
 */
struct expansion_work {
  struct k_work_delayable work;
  const char *expanded_text;
  uint8_t backspace_count;
  size_t text_index;
  int64_t start_time_ms;
  enum expansion_state state;
  uint32_t current_keycode;
  bool current_char_needs_shift;
  bool shift_mod_active;
};

/**
 * @brief The core work handler for the expansion state machine.
 *
 * @param work Pointer to the k_work item.
 */
void expansion_work_handler(struct k_work *work);

/**
 * @brief Starts a new text expansion process.
 *
 * This will cancel any ongoing expansion and begin a new one.
 *
 * @param work_item Pointer to the expansion_work struct to use.
 * @param short_code The short code that triggered the expansion.
 * @param expanded_text The full text to be typed out.
 * @param short_len The number of characters in the short code to backspace over.
 * @return 0 on success, negative error code on failure.
 */
int start_expansion(struct expansion_work *work_item, const char *short_code, const char *expanded_text, uint8_t short_len);

/**
 * @brief Immediately cancels the currently active expansion work.
 *
 * @param work_item Pointer to the expansion_work struct to cancel.
 */
void cancel_current_expansion(struct expansion_work *work_item);

#endif /* ZMK_EXPANSION_ENGINE_H */
