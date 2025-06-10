#ifndef ZMK_TEXT_EXPANDER_H
#define ZMK_TEXT_EXPANDER_H

#include <zephyr/kernel.h>
#include <stdbool.h>
#include <stdint.h>
#include <zephyr/device.h>

#include <zmk/trie.h>
#include <zmk/expansion_engine.h>
#include "generated_trie.h"

#if ZMK_TEXT_EXPANDER_GENERATED_MAX_SHORT_LEN > 0
#define MAX_SHORT_LEN (ZMK_TEXT_EXPANDER_GENERATED_MAX_SHORT_LEN + 1)
#else
#define MAX_SHORT_LEN 16
#endif

#define TYPING_DELAY CONFIG_ZMK_TEXT_EXPANDER_TYPING_DELAY
#define KEY_EVENT_QUEUE_SIZE CONFIG_ZMK_TEXT_EXPANDER_EVENT_QUEUE_SIZE

struct text_expander_key_event {
    uint16_t keycode;
    bool pressed;
};

struct text_expander_data {
  const struct trie_node *root;
  char current_short[MAX_SHORT_LEN];
  uint8_t current_short_len;
  struct k_mutex mutex;
  struct expansion_work expansion_work_item;
  struct k_msgq key_event_msgq;
  char key_event_msgq_buffer[KEY_EVENT_QUEUE_SIZE * sizeof(struct text_expander_key_event)];

#if DT_INST_NODE_HAS_PROP(0, undo_keycodes)
  char last_short_code[MAX_SHORT_LEN];
  const char *last_expanded_text;
  uint16_t last_trigger_keycode;
  bool just_expanded;
#endif
};

extern struct text_expander_data expander_data;

#endif /* ZMK_TEXT_EXPANDER_H */
