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
void text_expander_processor_work_handler(struct k_work *work);
K_WORK_DEFINE(text_expander_processor_work, text_expander_processor_work_handler);

static bool is_alpha(uint16_t keycode) {
    return (keycode >= HID_USAGE_KEY_KEYBOARD_A && keycode <= HID_USAGE_KEY_KEYBOARD_Z);
}

static bool is_numeric(uint16_t keycode) {
    return (keycode >= HID_USAGE_KEY_KEYBOARD_1_AND_EXCLAMATION && keycode <= HID_USAGE_KEY_KEYBOARD_9_AND_LEFT_PARENTHESIS) ||
           (keycode == HID_USAGE_KEY_KEYBOARD_0_AND_RIGHT_PARENTHESIS);
}

static bool is_short_code_char(uint16_t keycode) {
    return is_alpha(keycode) || is_numeric(keycode);
}

static bool is_modifier(uint16_t keycode) {
    return (keycode >= HID_USAGE_KEY_KEYBOARD_LEFTCONTROL && keycode <= HID_USAGE_KEY_KEYBOARD_RIGHT_GUI);
}

static bool is_ignorable_for_reset(uint16_t keycode) {
    if (is_modifier(keycode) || keycode == HID_USAGE_KEY_KEYBOARD_DELETE_BACKSPACE) {
        return true;
    }
    if (!IS_ENABLED(CONFIG_ZMK_TEXT_EXPANDER_RESET_ON_ENTER) && keycode == HID_USAGE_KEY_KEYBOARD_RETURN_ENTER) {
        return true;
    }
    if (!IS_ENABLED(CONFIG_ZMK_TEXT_EXPANDER_RESET_ON_TAB) && keycode == HID_USAGE_KEY_KEYBOARD_TAB) {
        return true;
    }
    return false;
}

static char keycode_to_short_code_char(uint16_t keycode) {
    if (is_alpha(keycode)) {
        return 'a' + (keycode - HID_USAGE_KEY_KEYBOARD_A);
    }
    if (keycode == HID_USAGE_KEY_KEYBOARD_0_AND_RIGHT_PARENTHESIS) {
        return '0';
    }
    return '1' + (keycode - HID_USAGE_KEY_KEYBOARD_1_AND_EXCLAMATION);
}

static void reset_current_short(void) {
    LOG_DBG("Resetting current short code. Was: '%s'", expander_data.current_short);
    memset(expander_data.current_short, 0, MAX_SHORT_LEN);
    expander_data.current_short_len = 0;
}

static void add_to_current_short(char c) {
    if (expander_data.current_short_len < MAX_SHORT_LEN - 1) {
        expander_data.current_short[expander_data.current_short_len++] = c;
        expander_data.current_short[expander_data.current_short_len] = '\0';
        LOG_DBG("Added '%c' to short code, now: '%s'", c, expander_data.current_short);
    } else {
        LOG_WRN("Short code buffer full, resetting. Current short: '%s'", expander_data.current_short);
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

static void check_for_aggressive_reset(char trigger_char) {
    if (!IS_ENABLED(CONFIG_ZMK_TEXT_EXPANDER_AGGRESSIVE_RESET_MODE) || trigger_char == 0) {
        return;
    }

    if (expander_data.current_short_len > 0 &&
        trie_get_node_for_key(expander_data.root, expander_data.current_short) == NULL) {
        LOG_INF("Aggressive reset: '%s' is not a prefix. Resetting.", expander_data.current_short);
        reset_current_short();
        if (IS_ENABLED(CONFIG_ZMK_TEXT_EXPANDER_RESTART_AFTER_RESET_WITH_TRIGGER_CHAR)) {
            add_to_current_short(trigger_char);
        }
    }
}

static int text_expander_load_expansion(const char *short_code, const char *expanded_text) {
    LOG_DBG("Loading expansion: '%s' -> '%s'", short_code, expanded_text);
    if (!short_code || !expanded_text) {
        LOG_ERR("Short code or expanded text is NULL");
        return -EINVAL;
    }
    size_t short_len = strlen(short_code);
    size_t expanded_len = strlen(expanded_text);

    if (short_len == 0 || short_len >= MAX_SHORT_LEN) {
        LOG_ERR("Invalid length for short code. Short: %zu/%d", short_len, MAX_SHORT_LEN);
        return -EINVAL;
    }
    if (expanded_len == 0 || expanded_len >= MAX_EXPANDED_LEN) {
        LOG_ERR("Invalid length for expanded text. Expanded: %zu/%d", expanded_len, MAX_EXPANDED_LEN);
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
    struct trie_node *node = trie_search(expander_data.root, short_code);
    bool is_update = (node != NULL);
    int ret = trie_insert(expander_data.root, short_code, expanded_text, &expander_data);
    if (ret == 0) {
        if (!is_update) {
            expander_data.expansion_count++;
            LOG_INF("Loaded new expansion ('%s'), count: %d", short_code, expander_data.expansion_count);
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
    if (ev == NULL) {
        return ZMK_EV_EVENT_BUBBLE;
    }
    
    struct text_expander_key_event key_event = { .keycode = ev->keycode, .pressed = ev->state };
    int ret = k_msgq_put(&expander_data.key_event_msgq, &key_event, K_NO_WAIT);
    if (ret != 0) {
        LOG_WRN("Failed to queue key event for text expander: %d", ret);
    } else {
        k_work_submit(&text_expander_processor_work);
    }
    
    return ZMK_EV_EVENT_BUBBLE;
}

void text_expander_processor_work_handler(struct k_work *work) {
    struct text_expander_key_event ev;

    while (k_msgq_get(&expander_data.key_event_msgq, &ev, K_NO_WAIT) == 0) {
        if (!ev.pressed) continue;

        k_mutex_lock(&expander_data.mutex, K_FOREVER);

        uint16_t keycode = ev.keycode;
        LOG_DBG("Processing keycode: %d", keycode);

        char char_that_caused_change = 0;
        bool reset_triggered = false;

        if (is_short_code_char(keycode)) {
            char_that_caused_change = keycode_to_short_code_char(keycode);
            add_to_current_short(char_that_caused_change);
            check_for_aggressive_reset(char_that_caused_change);
        } else if (keycode == HID_USAGE_KEY_KEYBOARD_DELETE_BACKSPACE) {
            if (expander_data.current_short_len > 0) {
                LOG_DBG("Backspace pressed, removing char from short code.");
                expander_data.current_short_len--;
                expander_data.current_short[expander_data.current_short_len] = '\0';
                LOG_DBG("Short code now: '%s'", expander_data.current_short);
            }
        } else if (!is_ignorable_for_reset(keycode)) {
            LOG_DBG("Reset-triggering key pressed (keycode: %d)", keycode);
            reset_triggered = true;
        }
        
        if (reset_triggered && expander_data.current_short_len > 0) {
            reset_current_short();
        }

        k_mutex_unlock(&expander_data.mutex);
    }
}

static int text_expander_keymap_binding_pressed(struct zmk_behavior_binding *binding,
                                                struct zmk_behavior_binding_event binding_event) {
    LOG_INF("Expansion trigger pressed, current short: '%s'", expander_data.current_short);
    k_mutex_lock(&expander_data.mutex, K_FOREVER);
    if (expander_data.current_short_len > 0) {
        struct trie_node *node = trie_search(expander_data.root, expander_data.current_short);
        if (node) {
            const char *expanded_ptr = node->expanded_text;
            char short_copy[MAX_SHORT_LEN];
            strncpy(short_copy, expander_data.current_short, sizeof(short_copy) - 1);
            short_copy[sizeof(short_copy) - 1] = '\0';

            LOG_INF("Found expansion: '%s' -> '%s'", short_copy, expanded_ptr);

            size_t short_len = strlen(short_copy);
            uint8_t len_to_delete = short_len;
            const char *text_for_engine = expanded_ptr;

            if (strncmp(expanded_ptr, short_copy, short_len) == 0) {
                text_for_engine = expanded_ptr + short_len;
                len_to_delete = 0;
                LOG_DBG("Optimized expansion, typing: '%s'", text_for_engine);
            }

            reset_current_short();
            
            int ret = start_expansion(&expander_data.expansion_work_item, short_copy, text_for_engine, len_to_delete);
            k_mutex_unlock(&expander_data.mutex);

            if (ret < 0) {
                LOG_ERR("Failed to start expansion: %d", ret);
            }
            return ZMK_BEHAVIOR_OPAQUE;
        } else {
            LOG_INF("No expansion found for '%s', resetting", expander_data.current_short);
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
    LOG_INF("Loading up to %zu expansions from config", config->expansion_count);
    int loaded_count = 0;
    int failed_count = 0;
    for (size_t i = 0; i < config->expansion_count; i++) {
        const struct text_expander_expansion *exp = &config->expansions[i];
        if (!exp->short_code || !exp->expanded_text) {
            LOG_WRN("Skipping expansion with NULL short code or text at index %zu", i);
            continue;
        }
        if (exp->short_code[0] == '\0' || exp->expanded_text[0] == '\0') {
            LOG_WRN("Skipping empty expansion at index %zu", i);
            continue;
        }
        int ret = text_expander_load_expansion(exp->short_code, exp->expanded_text);
        if (ret == 0) {
            loaded_count++;
        } else {
            failed_count++;
        }
    }
    LOG_INF("Successfully loaded %d expansions.", loaded_count);
    if (failed_count > 0) {
        LOG_WRN("Failed to load %d expansions. Check memory pool sizes (e.g., CONFIG_ZMK_TEXT_EXPANDER_MAX_EXPANSIONS).", failed_count);
    }
    return loaded_count;
}

static int text_expander_init(const struct device *dev) {
    const struct text_expander_config *config = dev->config;
    if (!zmk_text_expander_global_initialized) {
        LOG_INF("Initializing text expander module (first instance)");
        k_mutex_init(&expander_data.mutex);
        expander_data.node_pool_used = 0;
        expander_data.text_pool_used = 0;
        expander_data.expansion_count = 0;

#if defined(CONFIG_ZMK_TEXT_EXPANDER_ULTRA_LOW_MEMORY)
        expander_data.child_link_pool_used = 0;
#endif
    
        k_msgq_init(&expander_data.key_event_msgq, expander_data.key_event_msgq_buffer,
                    sizeof(struct text_expander_key_event), KEY_EVENT_QUEUE_SIZE);

        memset(expander_data.current_short, 0, MAX_SHORT_LEN);
        expander_data.current_short_len = 0;
        expander_data.root = trie_allocate_node(&expander_data);
        if (!expander_data.root) {
            LOG_ERR("Failed to allocate trie root node");
            return -ENOMEM;
        }

        k_work_init_delayable(&expander_data.expansion_work_item.work, expansion_work_handler);
        LOG_DBG("Initialized expansion work handler");

        zmk_text_expander_global_initialized = true;
    }
  
    load_expansions_from_config(config);

    if (expander_data.expansion_count == 0 && !IS_ENABLED(CONFIG_ZMK_TEXT_EXPANDER_NO_DEFAULT_EXPANSION)) {
        LOG_INF("No expansions loaded, adding default 'exp' -> 'expanded'");
        text_expander_load_expansion("exp", "expanded");
    }

    LOG_INF("Text expander initialization complete.");
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
