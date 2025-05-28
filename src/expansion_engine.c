#include <zephyr/kernel.h>
#include <string.h>
#include <zephyr/logging/log.h>

#include <zmk/expansion_engine.h>
#include <zmk/hid_utils.h>
#include <zmk/text_expander.h>

LOG_MODULE_REGISTER(expansion_engine, LOG_LEVEL_DBG);

static struct expansion_work expansion_work_item;

struct expansion_work *get_expansion_work_item(void) {
  return &expansion_work_item;
}

void cancel_current_expansion(void) {
    if (k_work_cancel_delayable(&expansion_work_item.work)) {
        LOG_DBG("Cancelled previous expansion work");
        expansion_work_item.state = EXPANSION_STATE_IDLE;
    }
}

void expansion_work_handler(struct k_work *work) {
  struct k_work_delayable *delayable_work = k_work_delayable_from_work(work);
  struct expansion_work *exp_work = CONTAINER_OF(delayable_work, struct expansion_work, work);

  int ret = 0;

  switch (exp_work->state) {
  case EXPANSION_STATE_START_BACKSPACE:
    LOG_DBG("State: START_BACKSPACE, backspace_count: %d", exp_work->backspace_count);
    if (exp_work->backspace_count > 0) {
        exp_work->state = EXPANSION_STATE_BACKSPACE_PRESS;
        k_work_reschedule(&exp_work->work, K_NO_WAIT);
    } else {
        exp_work->state = EXPANSION_STATE_START_TYPING;
        k_work_reschedule(&exp_work->work, K_MSEC(TYPING_DELAY * 2));
    }
    break;

  case EXPANSION_STATE_BACKSPACE_PRESS:
    LOG_DBG("State: BACKSPACE_PRESS");
    ret = send_and_flush_key_action(HID_USAGE_KEY_KEYBOARD_DELETE_BACKSPACE, true);
    if (ret < 0) {
        LOG_ERR("Failed to send backspace press: %d", ret);
        exp_work->state = EXPANSION_STATE_IDLE;
        break;
    }
    exp_work->state = EXPANSION_STATE_BACKSPACE_RELEASE;
    k_work_reschedule(&exp_work->work, K_MSEC(TYPING_DELAY / 2));
    break;

  case EXPANSION_STATE_BACKSPACE_RELEASE:
    LOG_DBG("State: BACKSPACE_RELEASE");
    send_and_flush_key_action(HID_USAGE_KEY_KEYBOARD_DELETE_BACKSPACE, false);
    exp_work->backspace_count--;
    if (exp_work->backspace_count > 0) {
        exp_work->state = EXPANSION_STATE_BACKSPACE_PRESS;
        k_work_reschedule(&exp_work->work, K_MSEC(TYPING_DELAY / 2));
    } else {
        exp_work->state = EXPANSION_STATE_START_TYPING;
        k_work_reschedule(&exp_work->work, K_MSEC(TYPING_DELAY * 2));
    }
    break;

  case EXPANSION_STATE_START_TYPING:
    LOG_DBG("State: START_TYPING");
    // Fallthrough
  case EXPANSION_STATE_TYPE_CHAR_START:
    LOG_DBG("State: TYPE_CHAR_START, index: %d", exp_work->text_index);
    if (exp_work->expanded_text[exp_work->text_index] != '\0' &&
        exp_work->text_index < CONFIG_ZMK_TEXT_EXPANDER_MAX_EXPANDED_LEN) {

      char c = exp_work->expanded_text[exp_work->text_index];
      exp_work->current_keycode = char_to_keycode(c, &exp_work->current_char_needs_shift);
      LOG_DBG("Typing char: '%c' (keycode: %d, shift: %d)", c, exp_work->current_keycode, exp_work->current_char_needs_shift);

      if (exp_work->current_keycode == 0) {
          exp_work->text_index++;
          exp_work->state = EXPANSION_STATE_TYPE_CHAR_START;
          k_work_reschedule(&exp_work->work, K_MSEC(TYPING_DELAY));
          break;
      }
      
      if (exp_work->current_char_needs_shift) {
        exp_work->state = EXPANSION_STATE_TYPE_CHAR_SHIFT_PRESS;
      } else {
        exp_work->state = EXPANSION_STATE_TYPE_CHAR_KEY_PRESS;
      }
      k_work_reschedule(&exp_work->work, K_NO_WAIT);

    } else {
        exp_work->state = EXPANSION_STATE_FINISH;
        k_work_reschedule(&exp_work->work, K_NO_WAIT);
    }
    break;

  case EXPANSION_STATE_TYPE_CHAR_SHIFT_PRESS:
    LOG_DBG("State: TYPE_CHAR_SHIFT_PRESS");
    ret = send_and_flush_key_action(HID_USAGE_KEY_KEYBOARD_LEFTSHIFT, true);
    if (ret < 0) {
        LOG_ERR("Failed to send shift press: %d", ret);
        exp_work->state = EXPANSION_STATE_IDLE;
        break;
    }
    exp_work->state = EXPANSION_STATE_TYPE_CHAR_KEY_PRESS;
    k_work_reschedule(&exp_work->work, K_MSEC(TYPING_DELAY / 4));
    break;
  
  case EXPANSION_STATE_TYPE_CHAR_KEY_PRESS:
    LOG_DBG("State: TYPE_CHAR_KEY_PRESS");
    ret = send_and_flush_key_action(exp_work->current_keycode, true);
    if (ret < 0) {
        LOG_ERR("Failed to send key press: %d", ret);
        exp_work->state = EXPANSION_STATE_IDLE;
        if(exp_work->current_char_needs_shift) send_and_flush_key_action(HID_USAGE_KEY_KEYBOARD_LEFTSHIFT, false);
        break;
    }
    exp_work->state = EXPANSION_STATE_TYPE_CHAR_KEY_RELEASE;
    k_work_reschedule(&exp_work->work, K_MSEC(TYPING_DELAY / 2));
    break;

  case EXPANSION_STATE_TYPE_CHAR_KEY_RELEASE:
    LOG_DBG("State: TYPE_CHAR_KEY_RELEASE");
    send_and_flush_key_action(exp_work->current_keycode, false);
     if (exp_work->current_char_needs_shift) {
        exp_work->state = EXPANSION_STATE_TYPE_CHAR_SHIFT_RELEASE;
    } else {
        exp_work->text_index++;
        exp_work->state = EXPANSION_STATE_TYPE_CHAR_START;
    }
    k_work_reschedule(&exp_work->work, K_MSEC(TYPING_DELAY / 2));
    break;

  case EXPANSION_STATE_TYPE_CHAR_SHIFT_RELEASE:
    LOG_DBG("State: TYPE_CHAR_SHIFT_RELEASE");
    send_and_flush_key_action(HID_USAGE_KEY_KEYBOARD_LEFTSHIFT, false);
    exp_work->text_index++;
    exp_work->state = EXPANSION_STATE_TYPE_CHAR_START;
    k_work_reschedule(&exp_work->work, K_MSEC(TYPING_DELAY / 4));
    break;
  
  case EXPANSION_STATE_FINISH:
    LOG_DBG("State: FINISH");
  case EXPANSION_STATE_IDLE:
  default:
    LOG_DBG("State: IDLE");
    exp_work->state = EXPANSION_STATE_IDLE;
    break;
  }
}

int start_expansion(const char *short_code, const char *expanded_text, uint8_t short_len) {
  LOG_DBG("Starting expansion for '%s' -> '%s' (delete %d chars)", short_code, expanded_text, short_len);
  cancel_current_expansion();

  strncpy(expansion_work_item.expanded_text, expanded_text,
          CONFIG_ZMK_TEXT_EXPANDER_MAX_EXPANDED_LEN - 1);
  expansion_work_item.expanded_text[CONFIG_ZMK_TEXT_EXPANDER_MAX_EXPANDED_LEN - 1] = '\0';

  expansion_work_item.backspace_count = short_len;
  expansion_work_item.text_index = 0;
  expansion_work_item.start_time_ms = k_uptime_get();

  if (expansion_work_item.backspace_count > 0) {
      expansion_work_item.state = EXPANSION_STATE_START_BACKSPACE;
  } else {
      expansion_work_item.state = EXPANSION_STATE_START_TYPING;
  }
  
  k_work_reschedule(&expansion_work_item.work, K_MSEC(10));
  return 0;
}
