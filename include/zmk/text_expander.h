#ifndef ZMK_TEXT_EXPANDER_H
#define ZMK_TEXT_EXPANDER_H

#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>
#include <stdbool.h>
#include <stdint.h>

#ifndef CONFIG_ZMK_TEXT_EXPANDER_MAX_EXPANSIONS
#define CONFIG_ZMK_TEXT_EXPANDER_MAX_EXPANSIONS 10
#endif
#ifndef CONFIG_ZMK_TEXT_EXPANDER_MAX_SHORT_LEN
#define CONFIG_ZMK_TEXT_EXPANDER_MAX_SHORT_LEN 16
#endif
#ifndef CONFIG_ZMK_TEXT_EXPANDER_MAX_EXPANDED_LEN
#define CONFIG_ZMK_TEXT_EXPANDER_MAX_EXPANDED_LEN 256
#endif
#ifndef CONFIG_ZMK_TEXT_EXPANDER_TYPING_DELAY
#define CONFIG_ZMK_TEXT_EXPANDER_TYPING_DELAY 10
#endif
#ifndef CONFIG_ZMK_TEXT_EXPANDER_EVENT_QUEUE_SIZE
#define CONFIG_ZMK_TEXT_EXPANDER_EVENT_QUEUE_SIZE 16
#endif

#define MAX_EXPANSIONS CONFIG_ZMK_TEXT_EXPANDER_MAX_EXPANSIONS
#define MAX_SHORT_LEN CONFIG_ZMK_TEXT_EXPANDER_MAX_SHORT_LEN
#define MAX_EXPANDED_LEN CONFIG_ZMK_TEXT_EXPANDER_MAX_EXPANDED_LEN
#define TYPING_DELAY CONFIG_ZMK_TEXT_EXPANDER_TYPING_DELAY
#define KEY_EVENT_QUEUE_SIZE CONFIG_ZMK_TEXT_EXPANDER_EVENT_QUEUE_SIZE

#include <zmk/trie.h>
#include <zmk/events/keycode_state_changed.h>

struct text_expander_key_event {
    uint16_t keycode;
    bool pressed;
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
  EXPANSION_STATE_FINISH,
};

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
 * @brief Main data structure for the text expander feature.
 *
 * @note This struct is intended to be a singleton, globally initialized once.
 * All text expander behaviors share this common data structure.
 */
struct text_expander_data {
  /** @brief Root of the trie used for storing and looking up expansions. */
  struct trie_node *root;
  /** @brief Buffer to hold the currently typed short code sequence. */
  char current_short[MAX_SHORT_LEN];
  /** @brief Current length of the string in current_short. */
  uint8_t current_short_len;
  /** @brief The total number of expansions currently loaded. */
  uint8_t expansion_count;
  /** @brief Mutex to protect concurrent access to shared data. */
  struct k_mutex mutex;

  /** @brief Work item for handling the asynchronous expansion (typing) process. */
  struct expansion_work expansion_work_item;

  /** @brief Message queue for key press events to be processed asynchronously. */
  struct k_msgq key_event_msgq;
  /** @brief Backing buffer for the key event message queue. */
  char key_event_msgq_buffer[KEY_EVENT_QUEUE_SIZE * sizeof(struct text_expander_key_event)];

  /** @brief Memory pool for trie nodes. */
  struct trie_node node_pool[MAX_EXPANSIONS * MAX_SHORT_LEN];
  /** @brief Number of nodes currently used in the node_pool. */
  uint16_t node_pool_used;

#if defined(CONFIG_ZMK_TEXT_EXPANDER_ULTRA_LOW_MEMORY)
  /** @brief Memory pool for child links in low memory mode. */
  struct trie_child_link child_link_pool[MAX_EXPANSIONS * MAX_SHORT_LEN];
  /** @brief Number of links currently used in the child_link_pool. */
  uint16_t child_link_pool_used;
#endif

  /** @brief Memory pool for storing the expanded text strings. */
  char text_pool[MAX_EXPANSIONS * MAX_EXPANDED_LEN];
  /** @brief Amount of memory used in the text_pool. */
  uint16_t text_pool_used;
};

extern struct text_expander_data expander_data;

#endif
