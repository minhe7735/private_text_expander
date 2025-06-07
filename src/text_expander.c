#define DT_DRV_COMPAT zmk_behavior_text_expander

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>
#include <drivers/behavior.h>
#include <string.h>
#include <zephyr/logging/log.h>

#include <zmk/behavior.h>
#include <zmk/event_manager.h>
#include <zmk/events/keycode_state_changed.h>
#include <zmk/hid.h>

#include <zmk/text_expander.h>
#include <zmk/trie.h>
#include <zmk/expansion_engine.h>

LOG_MODULE_REGISTER(text_expander, LOG_LEVEL_DBG);

struct text_expander_data expander_data;

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
    LOG_DBG("Keycode %d is not ignorable for reset", keycode);
    return false;
}

static char keycode_to_short_code_char(uint16_t keycode) {
    if (is_alpha(keycode)) {
        return 'a' + (keycode - HID_USAGE_KEY_KEYBOARD_A);
    }
    if (is_numeric(keycode)) {
        if (keycode == HID_USAGE_KEY_KEYBOARD_0_AND_RIGHT_PARENTHESIS) {
            return '0';
        }
        return '1' + (keycode - HID_USAGE_KEY_KEYBOARD_1_AND_EXCLAMATION);
    }
    return '\0';
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

static int text_expander_keycode_state_changed_listener(const zmk_event_t *eh) {
    struct zmk_keycode_state_changed *ev = as_zmk_keycode_state_changed(eh);
    if (ev == NULL) {
        return ZMK_EV_EVENT_BUBBLE;
    }
    
    if (expander_data.expansion_work_item.state != EXPANSION_STATE_IDLE) {
        return ZMK_EV_EVENT_BUBBLE;
    }

    LOG_DBG("Keycode event: keycode=%d, pressed=%s", ev->keycode, ev->state ? "true" : "false");
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
        if (!ev.pressed) {
            LOG_DBG("Ignoring key release event for keycode: %d", ev.keycode);
            continue;
        }

        k_mutex_lock(&expander_data.mutex, K_FOREVER);

        uint16_t keycode = ev.keycode;
        LOG_DBG("Processing key press: keycode=%d", keycode);

        char next_char = keycode_to_short_code_char(keycode);
        if (next_char != '\0') {
            if (IS_ENABLED(CONFIG_ZMK_TEXT_EXPANDER_AGGRESSIVE_RESET_MODE) && expander_data.current_short_len > 0) {
                char temp_short[MAX_SHORT_LEN];
                strncpy(temp_short, expander_data.current_short, MAX_SHORT_LEN - 1);
                temp_short[expander_data.current_short_len] = next_char;
                temp_short[expander_data.current_short_len + 1] = '\0';

                if (trie_get_node_for_key(temp_short) == NULL) {
                    LOG_INF("Aggressive reset: '%s' is not a prefix. Resetting.", temp_short);
                    reset_current_short();
                    if (IS_ENABLED(CONFIG_ZMK_TEXT_EXPANDER_RESTART_AFTER_RESET_WITH_TRIGGER_CHAR)) {
                        add_to_current_short(next_char);
                    }
                    k_mutex_unlock(&expander_data.mutex);
                    continue;
                }
            }
            add_to_current_short(next_char);
        } else if (keycode == HID_USAGE_KEY_KEYBOARD_DELETE_BACKSPACE) {
            if (expander_data.current_short_len > 0) {
                LOG_DBG("Backspace pressed, removing char from short code.");
                expander_data.current_short_len--;
                expander_data.current_short[expander_data.current_short_len] = '\0';
                LOG_DBG("Short code now: '%s'", expander_data.current_short);
            }
        } else if (!is_ignorable_for_reset(keycode)) {
            LOG_DBG("Reset-triggering key pressed (keycode: %d)", keycode);
            if (expander_data.current_short_len > 0) {
                reset_current_short();
            }
        }

        k_mutex_unlock(&expander_data.mutex);
    }
}

static int text_expander_keymap_binding_pressed(struct zmk_behavior_binding *binding,
                                                struct zmk_behavior_binding_event binding_event) {
    LOG_INF("Expansion trigger pressed, current short: '%s'", expander_data.current_short);
    k_mutex_lock(&expander_data.mutex, K_FOREVER);
    if (expander_data.current_short_len > 0) {
        const struct trie_node *node = trie_search(expander_data.current_short);
        if (node) {
            const char *expanded_ptr = zmk_text_expander_get_string(node->expanded_text_offset);
            if (expanded_ptr) {
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
            }
        } else {
            LOG_INF("No expansion found for '%s', resetting", expander_data.current_short);
            reset_current_short();
        }
    } else {
        LOG_DBG("Trigger pressed but no short code entered.");
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

static int text_expander_init(const struct device *dev) {
    if (expander_data.root == NULL) {
        LOG_INF("Initializing text expander module");
        k_mutex_init(&expander_data.mutex);
        k_msgq_init(&expander_data.key_event_msgq, expander_data.key_event_msgq_buffer,
                    sizeof(struct text_expander_key_event), KEY_EVENT_QUEUE_SIZE);
        
        memset(expander_data.current_short, 0, MAX_SHORT_LEN);
        expander_data.current_short_len = 0;
        
        expander_data.root = zmk_text_expander_trie_root;
        if (expander_data.root == NULL) {
             LOG_WRN("Text expander trie root is NULL. No expansions were generated.");
        }
        
        k_work_init_delayable(&expander_data.expansion_work_item.work, expansion_work_handler);
    }
    LOG_INF("Text expander initialization complete for device %s.", dev->name);
    return 0;
}

BEHAVIOR_DT_INST_DEFINE(0, text_expander_init, NULL, &expander_data, NULL,
                          POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT,
                          &text_expander_driver_api);
