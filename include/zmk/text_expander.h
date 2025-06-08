
#ifndef ZMK_TEXT_EXPANDER_H
#define ZMK_TEXT_EXPANDER_H

#include <zephyr/kernel.h>
#include <stdbool.h>
#include <stdint.h>

#include <zmk/trie.h>
#include <zmk/expansion_engine.h>
#include "generated_trie.h" // Include the auto-generated header

// Use the generated length for the buffer, plus one for the null terminator.
// If no expansions are defined, default to a reasonable small size to prevent build errors.
#if ZMK_TEXT_EXPANDER_GENERATED_MAX_SHORT_LEN > 0
#define MAX_SHORT_LEN (ZMK_TEXT_EXPANDER_GENERATED_MAX_SHORT_LEN + 1)
#else
#define MAX_SHORT_LEN 16 
#endif

// These Kconfig options are still valid for controlling other behaviors.
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
};

extern struct text_expander_data expander_data;

#endif /* ZMK_TEXT_EXPANDER_H */

