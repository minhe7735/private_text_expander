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
  EXPANSION_STATE_REPLAY_KEY_PRESS,
  EXPANSION_STATE_REPLAY_KEY_RELEASE,
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
  volatile enum expansion_state state;
  uint16_t current_keycode;
  bool current_char_needs_shift;
  bool shift_mod_active;
  uint16_t trigger_keycode_to_replay;
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
 * @param len_to_delete The number of characters to backspace over.
 * @param trigger_keycode The keycode to replay after expansion (0 if none).
 * @return 0 on success, negative error code on failure.
 */
int start_expansion(struct expansion_work *work_item, const char *short_code, const char *expanded_text, uint8_t len_to_delete, uint16_t trigger_keycode);

/**
 * @brief Immediately cancels the currently active expansion work.
 *
 * @param work_item Pointer to the expansion_work struct to cancel.
 */
void cancel_current_expansion(struct expansion_work *work_item);

#endif /* ZMK_EXPANSION_ENGINE_H */
