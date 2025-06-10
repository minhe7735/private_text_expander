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
#include <zmk/keymap.h>

#include <zmk/text_expander.h>
#include <zmk/trie.h>
#include <zmk/expansion_engine.h>

LOG_MODULE_REGISTER(text_expander, LOG_LEVEL_DBG);

#define EXPANDER_INST DT_DRV_INST(0)

#define NO_EXTRA_BACKSPACES 0
#define ONE_EXTRA_BACKSPACE 1
#define NO_REPLAY_KEY 0
#define ZMK_HID_USAGE_ID_MASK 0xFFFF

static uint16_t extract_hid_usage(uint32_t zmk_hid_usage) {
    return (uint16_t)(zmk_hid_usage & ZMK_HID_USAGE_ID_MASK);
}

static const uint32_t reset_keycodes[] = DT_INST_PROP_OR(0, reset_keycodes, {});
static const uint32_t auto_expand_keycodes[] = DT_INST_PROP_OR(0, auto_expand_keycodes, {});

#if DT_INST_NODE_HAS_PROP(0, undo_keycodes)
static const uint32_t undo_keycodes[] = DT_INST_PROP_OR(0, undo_keycodes, {});
#endif

struct text_expander_data expander_data;

static void process_key_event(struct text_expander_key_event *ev);
static bool handle_undo(uint16_t keycode);
static void handle_alphanumeric(char next_char);
static void handle_backspace();
static void handle_auto_expand(uint16_t keycode);
static void handle_reset_key();
static void handle_other_key();

void text_expander_processor_work_handler(struct k_work *work);
K_WORK_DEFINE(text_expander_processor_work, text_expander_processor_work_handler);

static bool keycode_in_array(uint16_t keycode, const uint32_t* arr, size_t len) {
    for (int i = 0; i < len; i++) {
        uint16_t array_keycode = extract_hid_usage(arr[i]);
        if (array_keycode == keycode) {
            return true;
        }
    }
    return false;
}

static char keycode_to_short_code_char(uint16_t keycode) {
    if (keycode >= HID_USAGE_KEY_KEYBOARD_A && keycode <= HID_USAGE_KEY_KEYBOARD_Z) {
        return 'a' + (keycode - HID_USAGE_KEY_KEYBOARD_A);
    }
    if (keycode >= HID_USAGE_KEY_KEYBOARD_1_AND_EXCLAMATION && keycode <= HID_USAGE_KEY_KEYBOARD_9_AND_LEFT_PARENTHESIS) {
        return '1' + (keycode - HID_USAGE_KEY_KEYBOARD_1_AND_EXCLAMATION);
    }
    if (keycode == HID_USAGE_KEY_KEYBOARD_0_AND_RIGHT_PARENTHESIS) {
        return '0';
    }

    switch (keycode) {
    case HID_USAGE_KEY_KEYBOARD_MINUS_AND_UNDERSCORE: return '-';
    case HID_USAGE_KEY_KEYBOARD_EQUAL_AND_PLUS: return '=';
    case HID_USAGE_KEY_KEYBOARD_SLASH_AND_QUESTION_MARK: return '/';
    case HID_USAGE_KEY_KEYBOARD_SEMICOLON_AND_COLON: return ';';
    case HID_USAGE_KEY_KEYBOARD_APOSTROPHE_AND_QUOTE: return '\'';
    case HID_USAGE_KEY_KEYBOARD_GRAVE_ACCENT_AND_TILDE: return '`';
    case HID_USAGE_KEY_KEYBOARD_COMMA_AND_LESS_THAN: return ',';
    case HID_USAGE_KEY_KEYBOARD_PERIOD_AND_GREATER_THAN: return '.';
    default: return '\0';
    }
}

static void reset_current_short(void) {
    LOG_DBG("Resetting current short code. Was: '%s'", expander_data.current_short);
    memset(expander_data.current_short, 0, MAX_SHORT_LEN);
    expander_data.current_short_len = 0;
}

static bool trigger_expansion(const char *short_code, uint8_t backspace_extra, uint16_t trigger_keycode) {
    const struct trie_node *node = trie_search(short_code);
    if (!node) {
        return false;
    }

    const char *expanded_ptr = zmk_text_expander_get_string(node->expanded_text_offset);
    if (!expanded_ptr) {
        return false;
    }

    size_t short_len = strlen(short_code);
    uint8_t len_to_delete = short_len + backspace_extra;
    const char *text_for_engine = expanded_ptr;

    if (strncmp(expanded_ptr, short_code, short_len) == 0) {
        text_for_engine = expanded_ptr + short_len;
        len_to_delete = backspace_extra;
        LOG_INF("Found completion: '%s' -> '%s'", short_code, expanded_ptr);
    } else {
        LOG_INF("Found replacement: '%s' -> '%s'", short_code, expanded_ptr);
    }

    uint16_t keycode_to_replay = node->preserve_trigger ? trigger_keycode : NO_REPLAY_KEY;

#if DT_INST_NODE_HAS_PROP(0, undo_keycodes)
    strncpy(expander_data.last_short_code, short_code, MAX_SHORT_LEN - 1);
    expander_data.last_expanded_text = expanded_ptr;
    expander_data.last_trigger_keycode = keycode_to_replay;
    expander_data.just_expanded = true;
#endif

    reset_current_short();
    LOG_INF("HANDOFF: Passing text and replay_keycode '%d' to engine", keycode_to_replay);
    start_expansion(&expander_data.expansion_work_item, text_for_engine, len_to_delete, keycode_to_replay);

    return true;
}

static void add_to_current_short(char c) {
    if (expander_data.current_short_len < MAX_SHORT_LEN - 1) {
        expander_data.current_short[expander_data.current_short_len++] = c;
        expander_data.current_short[expander_data.current_short_len] = '\0';
        LOG_DBG("Added '%c' to short code, now: '%s'", c, expander_data.current_short);
    } else {
        LOG_WRN("Short code buffer full, resetting.");
        reset_current_short();
    }
}

static int text_expander_keycode_state_changed_listener(const zmk_event_t *eh) {
    struct zmk_keycode_state_changed *ev = as_zmk_keycode_state_changed(eh);
    if (ev == NULL || expander_data.expansion_work_item.state != EXPANSION_STATE_IDLE) {
        return ZMK_EV_EVENT_BUBBLE;
    }

    struct text_expander_key_event key_event = { .keycode = ev->keycode, .pressed = ev->state };
    if (k_msgq_put(&expander_data.key_event_msgq, &key_event, K_NO_WAIT) != 0) {
        LOG_WRN("Failed to queue key event");
    } else {
        k_work_submit(&text_expander_processor_work);
    }

    return ZMK_EV_EVENT_BUBBLE;
}

void text_expander_processor_work_handler(struct k_work *work) {
    struct text_expander_key_event ev;
    while (k_msgq_get(&expander_data.key_event_msgq, &ev, K_NO_WAIT) == 0) {
        process_key_event(&ev);
    }
}

static void process_key_event(struct text_expander_key_event *ev) {
    if (!ev->pressed) {
        return;
    }

    k_mutex_lock(&expander_data.mutex, K_FOREVER);

    if (handle_undo(ev->keycode)) {
        k_mutex_unlock(&expander_data.mutex);
        return;
    }

    char next_char = keycode_to_short_code_char(ev->keycode);

    if (next_char != '\0') {
        handle_alphanumeric(next_char);
    } else if (ev->keycode == HID_USAGE_KEY_KEYBOARD_DELETE_BACKSPACE) {
        handle_backspace();
    } else if (keycode_in_array(ev->keycode, auto_expand_keycodes, ARRAY_SIZE(auto_expand_keycodes))) {
        handle_auto_expand(ev->keycode);
    } else if (keycode_in_array(ev->keycode, reset_keycodes, ARRAY_SIZE(reset_keycodes))) {
        handle_reset_key();
    } else {
        handle_other_key();
    }

    k_mutex_unlock(&expander_data.mutex);
}

#if DT_INST_NODE_HAS_PROP(0, undo_keycodes)
static bool handle_undo(uint16_t keycode) {
    if (expander_data.just_expanded) {
        expander_data.just_expanded = false;
        if (keycode_in_array(keycode, undo_keycodes, ARRAY_SIZE(undo_keycodes))) {
            LOG_INF("Undo triggered. Restoring '%s'", expander_data.last_short_code);
            uint8_t undo_backspaces = strlen(expander_data.last_expanded_text);
            if (expander_data.last_trigger_keycode != 0) {
                undo_backspaces++;
            }
            reset_current_short();
            start_expansion(&expander_data.expansion_work_item, expander_data.last_short_code, undo_backspaces, NO_REPLAY_KEY);
            return true;
        }
    }
    return false;
}
#else
static bool handle_undo(uint16_t keycode) { return false; }
#endif

static void handle_alphanumeric(char next_char) {
    add_to_current_short(next_char);

    #ifdef CONFIG_ZMK_TEXT_EXPANDER_AGGRESSIVE_RESET_MODE
    if (expander_data.current_short_len > 0) {
        if (trie_get_node_for_key(expander_data.current_short) == NULL) {
            LOG_DBG("Aggressive reset triggered by '%c'. No such prefix.", next_char);
            reset_current_short();

            #ifdef CONFIG_ZMK_TEXT_EXPANDER_RESTART_AFTER_RESET_WITH_TRIGGER_CHAR
            LOG_DBG("Restarting new short code with '%c'", next_char);
            add_to_current_short(next_char);
            #endif
        }
    }
    #endif
}

static void handle_backspace() {
    if (expander_data.current_short_len > 0) {
        expander_data.current_short_len--;
        expander_data.current_short[expander_data.current_short_len] = '\0';
    }
}

static void handle_auto_expand(uint16_t keycode) {
    if (expander_data.current_short_len > 0) {
        if (!trigger_expansion(expander_data.current_short, ONE_EXTRA_BACKSPACE, keycode)) {
            reset_current_short();
        }
    }
}

static void handle_reset_key() {
    if (expander_data.current_short_len > 0) {
        reset_current_short();
    }
}

static void handle_other_key() {
    if (expander_data.current_short_len > 0) {
        reset_current_short();
    }
}


static int text_expander_keymap_binding_pressed(struct zmk_behavior_binding *binding, struct zmk_behavior_binding_event binding_event) {
    k_mutex_lock(&expander_data.mutex, K_FOREVER);

    if (expander_data.current_short_len > 0) {
        if (!trigger_expansion(expander_data.current_short, NO_EXTRA_BACKSPACES, NO_REPLAY_KEY)) {
            LOG_INF("No expansion found for '%s', resetting.", expander_data.current_short);
            reset_current_short();
        }
    } else {
        LOG_DBG("Manual trigger pressed but no short code entered.");
    }

    k_mutex_unlock(&expander_data.mutex);
    return ZMK_BEHAVIOR_OPAQUE;
}

static int text_expander_keymap_binding_released(struct zmk_behavior_binding *binding, struct zmk_behavior_binding_event binding_event) {
    return ZMK_BEHAVIOR_TRANSPARENT;
}

ZMK_LISTENER(text_expander_listener_interface, text_expander_keycode_state_changed_listener);
ZMK_SUBSCRIPTION(text_expander_listener_interface, zmk_keycode_state_changed);

static const struct behavior_driver_api text_expander_driver_api = {
    .binding_pressed = text_expander_keymap_binding_pressed,
    .binding_released = text_expander_keymap_binding_released,
};

static int text_expander_init(const struct device *dev) {
    static bool initialized = false;
    if (initialized) { return 0; }

    LOG_INF("Initializing ZMK Text Expander module");
    k_mutex_init(&expander_data.mutex);
    k_msgq_init(&expander_data.key_event_msgq, expander_data.key_event_msgq_buffer, sizeof(struct text_expander_key_event), KEY_EVENT_QUEUE_SIZE);

    reset_current_short();

#if DT_INST_NODE_HAS_PROP(0, undo_keycodes)
    expander_data.just_expanded = false;
    expander_data.last_expanded_text = NULL;
    expander_data.last_trigger_keycode = 0;
    memset(expander_data.last_short_code, 0, MAX_SHORT_LEN);
#endif

    if (zmk_text_expander_trie_num_nodes > 0) {
         expander_data.root = &zmk_text_expander_trie_nodes[0];
    } else {
         LOG_WRN("Text expander trie is empty. No expansions defined.");
    }

    k_work_init_delayable(&expander_data.expansion_work_item.work, expansion_work_handler);
    initialized = true;

    return 0;
}

BEHAVIOR_DT_INST_DEFINE(0, text_expander_init, NULL, &expander_data, NULL, POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &text_expander_driver_api);
