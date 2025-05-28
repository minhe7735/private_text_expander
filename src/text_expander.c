#define DT_DRV_COMPAT zmk_behavior_text_expander

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>
#include <drivers/behavior.h>
#include <errno.h>
#include <string.h>
#include <zephyr/logging/log.h>

#include <zmk/behavior.h>
#include <zmk/event_manager.h>
#include <zmk/events/keycode_state_changed.h>
#include <zmk/keymap.h>
#include <zmk/hid.h>

#include <zmk/text_expander.h>
#include <zmk/trie.h>
#include <zmk/expansion_engine.h>

LOG_MODULE_REGISTER(text_expander, LOG_LEVEL_DBG);

struct text_expander_data expander_data;

struct text_expander_expansion {
  const char *short_code;
  const char *expanded_text;
};

struct text_expander_config {
  const struct text_expander_expansion *expansions;
  size_t expansion_count;
};

static bool zmk_text_expander_global_initialized = false;

static const char *find_expansion(const char *short_code) {
  LOG_DBG("Finding expansion for '%s'", short_code);
  struct trie_node *node = trie_search(expander_data.root, short_code);
  const char *result = trie_get_expanded_text(node);
  LOG_DBG("Found: %s", result ? result : "NULL");
  return result;
}

static void reset_current_short(void) {
  LOG_DBG("Resetting current short code");
  memset(expander_data.current_short, 0, MAX_SHORT_LEN);
  expander_data.current_short_len = 0;
}

static void add_to_current_short(char c) {
  if (expander_data.current_short_len < MAX_SHORT_LEN - 1) {
    expander_data.current_short[expander_data.current_short_len++] = c;
    expander_data.current_short[expander_data.current_short_len] = '\0';
    LOG_DBG("Added '%c' to short code, now: '%s'", c, expander_data.current_short);
  } else {
    LOG_WRN("Short code buffer full, resetting");
    reset_current_short();
    if (IS_ENABLED(CONFIG_ZMK_TEXT_EXPANDER_RESTART_AFTER_RESET_WITH_TRIGGER_CHAR)) {
        if (MAX_SHORT_LEN > 1) {
          expander_data.current_short[expander_data.current_short_len++] = c;
          expander_data.current_short[expander_data.current_short_len] = '\0';
          LOG_DBG("Restarted with '%c', now: '%s'", c, expander_data.current_short);
        }
    }
  }
}

static int text_expander_load_expansion_internal(const char *short_code, const char *expanded_text) {
  LOG_DBG("Loading expansion: '%s' -> '%s'", short_code, expanded_text);
  if (!short_code || !expanded_text) {
    LOG_ERR("Short code or expanded text is NULL");
    return -EINVAL;
  }
  size_t short_len = strlen(short_code);
  size_t expanded_len = strlen(expanded_text);

  if (short_len == 0 || short_len >= MAX_SHORT_LEN || expanded_len == 0 ||
      expanded_len >= MAX_EXPANDED_LEN) {
    LOG_ERR("Invalid length for short code or expanded text");
    return -EINVAL;
  }
  for (int i = 0; short_code[i] != '\0'; i++) {
    char c = short_code[i];
    if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'))) {
      LOG_ERR("Invalid character in short code: '%c'", c);
      return -EINVAL;
    }
  }
  k_mutex_lock(&expander_data.mutex, K_FOREVER);
  bool is_update = (find_expansion(short_code) != NULL);
  int ret = trie_insert(expander_data.root, short_code, expanded_text, &expander_data);
  if (ret == 0) {
    if (!is_update) {
      expander_data.expansion_count++;
      LOG_INF("Loaded new expansion, count: %d", expander_data.expansion_count);
    } else {
      LOG_INF("Updated existing expansion for '%s'", short_code);
    }
  } else {
    LOG_ERR("Failed to insert expansion into trie: %d", ret);
  }
  k_mutex_unlock(&expander_data.mutex);
  return ret;
}

static int text_expander_keycode_state_changed_listener(const zmk_event_t *eh) {
  struct zmk_keycode_state_changed *ev = as_zmk_keycode_state_changed(eh);

  if (ev == NULL || !ev->state) {
    return ZMK_EV_EVENT_BUBBLE;
  }
  if (k_mutex_lock(&expander_data.mutex, K_NO_WAIT) != 0) {
    return ZMK_EV_EVENT_BUBBLE;
  }
  uint16_t keycode = ev->keycode;
  LOG_DBG("Keycode state changed: %d", keycode);
  bool current_short_content_changed = false;
  char char_that_caused_change = 0;

  if (keycode >= HID_USAGE_KEY_KEYBOARD_A && keycode <= HID_USAGE_KEY_KEYBOARD_Z) {
    char_that_caused_change = 'a' + (keycode - HID_USAGE_KEY_KEYBOARD_A);
    add_to_current_short(char_that_caused_change);
    current_short_content_changed = true;
  } else if (keycode >= HID_USAGE_KEY_KEYBOARD_1_AND_EXCLAMATION &&
             keycode <= HID_USAGE_KEY_KEYBOARD_9_AND_LEFT_PARENTHESIS) {
    char_that_caused_change = '1' + (keycode - HID_USAGE_KEY_KEYBOARD_1_AND_EXCLAMATION);
    add_to_current_short(char_that_caused_change);
    current_short_content_changed = true;
  } else if (keycode == HID_USAGE_KEY_KEYBOARD_0_AND_RIGHT_PARENTHESIS) {
    char_that_caused_change = '0';
    add_to_current_short(char_that_caused_change);
    current_short_content_changed = true;
  } else if (keycode == HID_USAGE_KEY_KEYBOARD_DELETE_BACKSPACE) {
    if (expander_data.current_short_len > 0) {
      expander_data.current_short_len--;
      expander_data.current_short[expander_data.current_short_len] = '\0';
      current_short_content_changed = true;
      LOG_DBG("Backspace, short code now: '%s'", expander_data.current_short);
    }
  }

  if (IS_ENABLED(CONFIG_ZMK_TEXT_EXPANDER_AGGRESSIVE_RESET_MODE)) {
    if (current_short_content_changed && char_that_caused_change != 0 && expander_data.current_short_len > 0) {
      struct trie_node *node =
          trie_get_node_for_key(expander_data.root, expander_data.current_short);
      if (node == NULL) {
        LOG_DBG("Aggressive reset triggered for '%s'", expander_data.current_short);
        if (IS_ENABLED(CONFIG_ZMK_TEXT_EXPANDER_RESTART_AFTER_RESET_WITH_TRIGGER_CHAR)) {
            reset_current_short();
            add_to_current_short(char_that_caused_change);
        } else {
            reset_current_short();
        }
        current_short_content_changed = false;
      }
    }
  }

  if (keycode == HID_USAGE_KEY_KEYBOARD_SPACEBAR) {
    if (expander_data.current_short_len > 0) {
      LOG_DBG("Spacebar pressed, resetting short code");
      reset_current_short();
    }
  } else if (!current_short_content_changed &&
             !((keycode >= HID_USAGE_KEY_KEYBOARD_A && keycode <= HID_USAGE_KEY_KEYBOARD_Z) ||
               (keycode >= HID_USAGE_KEY_KEYBOARD_1_AND_EXCLAMATION &&
                keycode <= HID_USAGE_KEY_KEYBOARD_0_AND_RIGHT_PARENTHESIS) ||
               keycode == HID_USAGE_KEY_KEYBOARD_DELETE_BACKSPACE ||
               keycode == HID_USAGE_KEY_KEYBOARD_SPACEBAR ||
               keycode == HID_USAGE_KEY_KEYBOARD_LEFTSHIFT ||
               keycode == HID_USAGE_KEY_KEYBOARD_RIGHTSHIFT ||
               keycode == HID_USAGE_KEY_KEYBOARD_LEFTCONTROL ||
               keycode == HID_USAGE_KEY_KEYBOARD_RIGHTCONTROL ||
               keycode == HID_USAGE_KEY_KEYBOARD_LEFTALT ||
               keycode == HID_USAGE_KEY_KEYBOARD_RIGHTALT ||
               keycode == HID_USAGE_KEY_KEYBOARD_LEFT_GUI ||
               keycode == HID_USAGE_KEY_KEYBOARD_RIGHT_GUI ||
               (!IS_ENABLED(CONFIG_ZMK_TEXT_EXPANDER_RESET_ON_ENTER) &&
                keycode == HID_USAGE_KEY_KEYBOARD_RETURN_ENTER) ||
               (!IS_ENABLED(CONFIG_ZMK_TEXT_EXPANDER_RESET_ON_TAB) &&
                keycode == HID_USAGE_KEY_KEYBOARD_TAB))) {
    if (expander_data.current_short_len > 0) {
      LOG_DBG("Non-alphanumeric key pressed, resetting short code");
      reset_current_short();
    }
  }
  k_mutex_unlock(&expander_data.mutex);
  return ZMK_EV_EVENT_BUBBLE;
}

static int text_expander_keymap_binding_pressed(struct zmk_behavior_binding *binding,
                                                struct zmk_behavior_binding_event binding_event) {
  LOG_DBG("Binding pressed");
  k_mutex_lock(&expander_data.mutex, K_FOREVER);
  if (expander_data.current_short_len > 0) {
    const char *expanded_ptr = find_expansion(expander_data.current_short);
    if (expanded_ptr) {
      char expanded_copy[MAX_EXPANDED_LEN];
      char short_copy[MAX_SHORT_LEN];
      uint16_t expanded_len_val = trie_get_expanded_text_len_from_ptr(expanded_ptr);

      if (expanded_len_val > 0 && expanded_len_val < MAX_EXPANDED_LEN) {
          memcpy(expanded_copy, expanded_ptr, expanded_len_val);
          expanded_copy[expanded_len_val] = '\0';
      } else if (expanded_len_val >= MAX_EXPANDED_LEN) {
          strncpy(expanded_copy, expanded_ptr, sizeof(expanded_copy) - 1);
          expanded_copy[sizeof(expanded_copy) - 1] = '\0';
      } else {
          expanded_copy[0] = '\0';
      }

      strncpy(short_copy, expander_data.current_short, sizeof(short_copy) - 1);
      short_copy[sizeof(short_copy) - 1] = '\0';
      LOG_INF("Found expansion: '%s' -> '%s'", short_copy, expanded_copy);

      uint8_t len_to_delete_default = expander_data.current_short_len;
      const char *text_for_engine = expanded_copy;
      uint8_t final_len_to_delete = len_to_delete_default;
      size_t short_len_val_for_cmp = strlen(short_copy);

      if (short_len_val_for_cmp > 0 && expanded_len_val >= short_len_val_for_cmp && strncmp(expanded_copy, short_copy, short_len_val_for_cmp) == 0) {
          text_for_engine = expanded_copy + short_len_val_for_cmp;
          final_len_to_delete = 0;
          LOG_DBG("Optimized expansion, typing: '%s'", text_for_engine);
      }

      reset_current_short();
      k_mutex_unlock(&expander_data.mutex);
      int ret = start_expansion(short_copy, text_for_engine, final_len_to_delete);
      if (ret < 0) {
        LOG_ERR("Failed to start expansion: %d", ret);
        return ZMK_BEHAVIOR_OPAQUE;
      }
      return ZMK_BEHAVIOR_OPAQUE;
    } else {
      LOG_DBG("No expansion found for '%s', resetting", expander_data.current_short);
      reset_current_short();
    }
  }
  k_mutex_unlock(&expander_data.mutex);
  return ZMK_BEHAVIOR_TRANSPARENT;
}

static int text_expander_keymap_binding_released(struct zmk_behavior_binding *binding,
                                                 struct zmk_behavior_binding_event binding_event) {
  LOG_DBG("Binding released");
  return ZMK_BEHAVIOR_TRANSPARENT;
}

ZMK_LISTENER(text_expander_listener_interface, text_expander_keycode_state_changed_listener);
ZMK_SUBSCRIPTION(text_expander_listener_interface, zmk_keycode_state_changed);

static const struct behavior_driver_api text_expander_driver_api = {
    .binding_pressed = text_expander_keymap_binding_pressed,
    .binding_released = text_expander_keymap_binding_released,
};

static int load_expansions_from_config(const struct text_expander_config *config) {
  if (!config || !config->expansions || config->expansion_count == 0) {
    LOG_INF("No expansions to load from config");
    return 0;
  }
  LOG_INF("Loading %d expansions from config", config->expansion_count);
  int loaded_count = 0;
  for (size_t i = 0; i < config->expansion_count; i++) {
    const struct text_expander_expansion *exp = &config->expansions[i];
    if (!exp->short_code || !exp->expanded_text) {
      LOG_WRN("Skipping expansion with NULL short code or text");
      continue;
    }
    if (exp->short_code[0] == '\0' || exp->expanded_text[0] == '\0') {
      LOG_WRN("Skipping empty expansion");
      continue;
    }
    int ret = text_expander_load_expansion_internal(exp->short_code, exp->expanded_text);
    if (ret == 0) {
      loaded_count++;
    }
  }
  return loaded_count;
}

static int text_expander_init(const struct device *dev) {
  const struct text_expander_config *config = dev->config;
  if (!zmk_text_expander_global_initialized) {
    LOG_INF("Initializing text expander module");
    k_mutex_init(&expander_data.mutex);
    expander_data.node_pool_used = 0;
    expander_data.text_pool_used = 0;
    expander_data.expansion_count = 0;

    memset(expander_data.current_short, 0, MAX_SHORT_LEN);
    expander_data.current_short_len = 0;
    expander_data.root = trie_allocate_node(&expander_data);
    if (!expander_data.root) {
      LOG_ERR("Failed to allocate trie root node");
      return -ENOMEM;
    }

    struct expansion_work *work_item = get_expansion_work_item();
    if (work_item) {
      k_work_init_delayable(&work_item->work, expansion_work_handler);
      LOG_DBG("Initialized expansion work handler");
    }

    int loaded_count = load_expansions_from_config(config);
    if (loaded_count == 0 && expander_data.expansion_count == 0) {
      LOG_INF("No expansions loaded, adding default 'exp' -> 'expanded'");
      text_expander_load_expansion_internal("exp", "expanded");
    }

    zmk_text_expander_global_initialized = true;
    LOG_INF("Text expander initialized");
  } else {
    LOG_INF("Text expander already initialized, loading additional expansions");
    load_expansions_from_config(config);
  }
  return 0;
}

#define TEXT_EXPANDER_EXPANSION(node_id) \
  { \
    .short_code = DT_PROP_OR(node_id, short_code, ""), .expanded_text = \
        DT_PROP_OR(node_id, expanded_text, ""), \
  },

#define TEXT_EXPANDER_INST(n) \
  static const struct text_expander_expansion text_expander_expansions_##n[] = { \
      DT_INST_FOREACH_CHILD(n, TEXT_EXPANDER_EXPANSION)}; \
  static const struct text_expander_config text_expander_config_##n = { \
      .expansions = text_expander_expansions_##n, \
      .expansion_count = ARRAY_SIZE(text_expander_expansions_##n), \
  }; \
  BEHAVIOR_DT_INST_DEFINE(n, text_expander_init, NULL, &expander_data, &text_expander_config_##n, \
                          POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, \
                          &text_expander_driver_api);

DT_INST_FOREACH_STATUS_OKAY(TEXT_EXPANDER_INST)
