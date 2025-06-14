#include <zephyr/kernel.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <zephyr/logging/log.h>
#include <zmk/hid.h>
#include <zmk/endpoints.h>
#include <zmk/expansion_engine.h>
#include <zmk/hid_utils.h>
#include <zmk/text_expander.h>

LOG_MODULE_REGISTER(expansion_engine, LOG_LEVEL_DBG);

static void handle_start_backspace(struct expansion_work *exp_work);
static void handle_backspace_press(struct expansion_work *exp_work);
static void handle_backspace_release(struct expansion_work *exp_work);
static void handle_start_typing(struct expansion_work *exp_work);
static void handle_type_char_start(struct expansion_work *exp_work);
static void handle_type_literal_char(struct expansion_work *exp_work);
static void handle_type_char_key_press(struct expansion_work *exp_work);
static void handle_type_char_key_release(struct expansion_work *exp_work);
static void handle_finish(struct expansion_work *exp_work);
static void handle_replay_key_press(struct expansion_work *exp_work);
static void handle_replay_key_release(struct expansion_work *exp_work);
static void handle_unicode_dispatch(struct expansion_work *exp_work);
static uint32_t get_numpad_keycode(char digit);
static uint32_t get_hex_keycode(char hex_digit);

static void clear_shift_if_active(struct expansion_work *exp_work) {
    if (exp_work->shift_mod_active) {
        LOG_DBG("Clearing active shift modifier.");
        zmk_hid_unregister_mods(MOD_LSFT);
        zmk_endpoints_send_report(HID_USAGE_KEY);
        exp_work->shift_mod_active = false;
    }
}

void cancel_current_expansion(struct expansion_work *work_item) {
    if (k_work_cancel_delayable(&work_item->work) >= 0) {
        LOG_INF("Cancelling current expansion work.");
        if (work_item->current_keycode > 0) {
            LOG_DBG("Releasing potentially stuck keycode: 0x%04X", work_item->current_keycode);
            send_and_flush_key_action(work_item->current_keycode, false);
        }
        clear_shift_if_active(work_item);
        work_item->state = EXPANSION_STATE_IDLE;
        work_item->current_keycode = 0;
    }
}

void expansion_work_handler(struct k_work *work) {
    struct k_work_delayable *delayable_work = k_work_delayable_from_work(work);
    struct expansion_work *exp_work = CONTAINER_OF(delayable_work, struct expansion_work, work);

    LOG_DBG("Expansion engine state: %d", exp_work->state);

    switch (exp_work->state) {
        case EXPANSION_STATE_START_BACKSPACE:       handle_start_backspace(exp_work);      break;
        case EXPANSION_STATE_BACKSPACE_PRESS:       handle_backspace_press(exp_work);      break;
        case EXPANSION_STATE_BACKSPACE_RELEASE:     handle_backspace_release(exp_work);    break;
        case EXPANSION_STATE_START_TYPING:          handle_start_typing(exp_work);         break;
        case EXPANSION_STATE_TYPE_CHAR_START:       handle_type_char_start(exp_work);      break;
        case EXPANSION_STATE_TYPE_LITERAL_CHAR:     handle_type_literal_char(exp_work);    break;
        case EXPANSION_STATE_TYPE_CHAR_KEY_PRESS:   handle_type_char_key_press(exp_work);  break;
        case EXPANSION_STATE_TYPE_CHAR_KEY_RELEASE: handle_type_char_key_release(exp_work);break;
        case EXPANSION_STATE_FINISH:                handle_finish(exp_work);               break;
        case EXPANSION_STATE_REPLAY_KEY_PRESS:      handle_replay_key_press(exp_work);     break;
        case EXPANSION_STATE_REPLAY_KEY_RELEASE:    handle_replay_key_release(exp_work);   break;
        case EXPANSION_STATE_UNICODE_START:
        case EXPANSION_STATE_UNICODE_PRESS_MODS:
        case EXPANSION_STATE_UNICODE_RELEASE_MODS:
        case EXPANSION_STATE_UNICODE_PRESS_KEY:
        case EXPANSION_STATE_UNICODE_RELEASE_KEY:
        case EXPANSION_STATE_UNICODE_TYPE_HEX:
        case EXPANSION_STATE_UNICODE_PRESS_TERMINATOR:
        case EXPANSION_STATE_UNICODE_RELEASE_TERMINATOR:
            handle_unicode_dispatch(exp_work);
            break;
        default:
            LOG_WRN("Unhandled expansion state: %d. Setting to IDLE.", exp_work->state);
            exp_work->state = EXPANSION_STATE_IDLE;
            break;
    }
}

static void handle_start_backspace(struct expansion_work *exp_work) {
    if (exp_work->backspace_count > 0) {
        LOG_DBG("Starting backspace sequence, %d to go.", exp_work->backspace_count);
        exp_work->state = EXPANSION_STATE_BACKSPACE_PRESS;
        k_work_reschedule(&exp_work->work, K_NO_WAIT);
    } else {
        LOG_DBG("No backspaces needed, starting typing.");
        exp_work->state = EXPANSION_STATE_START_TYPING;
        k_work_reschedule(&exp_work->work, K_MSEC(TYPING_DELAY));
    }
}

static void handle_backspace_press(struct expansion_work *exp_work) {
    LOG_DBG("Pressing backspace");
    send_and_flush_key_action(HID_USAGE_KEY_KEYBOARD_DELETE_BACKSPACE, true);
    exp_work->state = EXPANSION_STATE_BACKSPACE_RELEASE;
    k_work_reschedule(&exp_work->work, K_MSEC(TYPING_DELAY / 2));
}

static void handle_backspace_release(struct expansion_work *exp_work) {
    LOG_DBG("Releasing backspace");
    send_and_flush_key_action(HID_USAGE_KEY_KEYBOARD_DELETE_BACKSPACE, false);
    exp_work->backspace_count--;
    exp_work->state = EXPANSION_STATE_START_BACKSPACE;
    k_work_reschedule(&exp_work->work, K_MSEC(TYPING_DELAY / 2));
}

static void handle_start_typing(struct expansion_work *exp_work) {
    LOG_DBG("Beginning to type expanded text.");
    handle_type_char_start(exp_work);
}

static uint8_t hex_char_to_val(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return 0;
}

static void handle_type_char_start(struct expansion_work *exp_work) {
    const char *text = exp_work->expanded_text;
    size_t len = strlen(text);

    if (exp_work->text_index >= len) {
        LOG_DBG("End of expansion string reached.");
        exp_work->state = EXPANSION_STATE_FINISH;
        k_work_reschedule(&exp_work->work, K_NO_WAIT);
        return;
    }

    if (strncmp(&text[exp_work->text_index], "{{{", 3) == 0) {
        LOG_DBG("Found literal block start: {{{");
        const char *end = strstr(&text[exp_work->text_index + 3], "}}}");
        if (end) {
            exp_work->text_index += 3;
            exp_work->literal_end_index = end - text;
            LOG_DBG("Literal block ends at index %d. Entering literal typing mode.", exp_work->literal_end_index);
            exp_work->state = EXPANSION_STATE_TYPE_LITERAL_CHAR;
            k_work_reschedule(&exp_work->work, K_NO_WAIT);
            return;
        }
    }

    if (strncmp(&text[exp_work->text_index], "{{", 2) == 0) {
        LOG_DBG("Found command block start: {{");
        const char *end = strstr(&text[exp_work->text_index + 2], "}}");
        if (end) {
            size_t cmd_len = end - (text + exp_work->text_index + 2);
            char cmd_buf[16];
            if (cmd_len >= sizeof(cmd_buf)) { cmd_len = sizeof(cmd_buf) - 1; }
            
            strncpy(cmd_buf, &text[exp_work->text_index + 2], cmd_len);
            cmd_buf[cmd_len] = '\0';
            exp_work->text_index += cmd_len + 4;

            LOG_DBG("Parsed command: \"%s\"", cmd_buf);

            if (strncmp(cmd_buf, "u:", 2) == 0) {
                exp_work->unicode_codepoint = strtol(&cmd_buf[2], NULL, 16);
                LOG_DBG("Parsed unicode command: U+%04X", exp_work->unicode_codepoint);
                exp_work->state = EXPANSION_STATE_UNICODE_START;
            } else if (strncmp(cmd_buf, "cmd:", 4) == 0) {
                if (strcmp(&cmd_buf[4], "win") == 0) expander_data.unicode_mode = TE_UNICODE_MODE_WINDOWS;
                else if (strcmp(&cmd_buf[4], "mac") == 0) expander_data.unicode_mode = TE_UNICODE_MODE_MACOS;
                else if (strcmp(&cmd_buf[4], "linux") == 0) expander_data.unicode_mode = TE_UNICODE_MODE_LINUX;
                LOG_INF("Set unicode mode to %d", expander_data.unicode_mode);
                exp_work->state = EXPANSION_STATE_TYPE_CHAR_START;
            } else {
                LOG_WRN("Unknown command: %s", cmd_buf);
                exp_work->state = EXPANSION_STATE_TYPE_CHAR_START;
            }

            k_work_reschedule(&exp_work->work, K_MSEC(TYPING_DELAY));
            return;
        }
    }

    char char_to_type = text[exp_work->text_index];
    exp_work->current_keycode = char_to_keycode(char_to_type, &exp_work->current_char_needs_shift);
    exp_work->state = EXPANSION_STATE_TYPE_CHAR_KEY_PRESS;
    k_work_reschedule(&exp_work->work, K_MSEC(1));
}

static void handle_type_literal_char(struct expansion_work *exp_work) {
    if (exp_work->text_index >= exp_work->literal_end_index) {
        LOG_DBG("End of literal block reached.");
        exp_work->text_index += 3;
        exp_work->literal_end_index = 0;
        exp_work->state = EXPANSION_STATE_TYPE_CHAR_START;
        k_work_reschedule(&exp_work->work, K_NO_WAIT);
        return;
    }
    
    char char_to_type = exp_work->expanded_text[exp_work->text_index];
    LOG_DBG("Typing literal character '%c'", char_to_type);
    exp_work->current_keycode = char_to_keycode(char_to_type, &exp_work->current_char_needs_shift);
    exp_work->state = EXPANSION_STATE_TYPE_CHAR_KEY_PRESS;
    k_work_reschedule(&exp_work->work, K_MSEC(1));
}

static void handle_type_char_key_press(struct expansion_work *exp_work) {
    if (exp_work->current_char_needs_shift && !exp_work->shift_mod_active) {
        zmk_hid_register_mods(MOD_LSFT);
        exp_work->shift_mod_active = true;
    } else if (!exp_work->current_char_needs_shift && exp_work->shift_mod_active) {
        zmk_hid_unregister_mods(MOD_LSFT);
        exp_work->shift_mod_active = false;
    }
    
    LOG_DBG("Pressing keycode 0x%04X", exp_work->current_keycode);
    if (exp_work->current_keycode > 0) {
        int ret = send_and_flush_key_action(exp_work->current_keycode, true);
        if (ret < 0) {
            LOG_ERR("Failed to send key press, aborting expansion.");
            clear_shift_if_active(exp_work);
            exp_work->state = EXPANSION_STATE_IDLE;
            return;
        }
    }
    exp_work->state = EXPANSION_STATE_TYPE_CHAR_KEY_RELEASE;
    k_work_reschedule(&exp_work->work, K_MSEC(TYPING_DELAY / 2));
}

static void handle_type_char_key_release(struct expansion_work *exp_work) {
    LOG_DBG("Releasing keycode 0x%04X", exp_work->current_keycode);
    if (exp_work->current_keycode > 0) {
        send_and_flush_key_action(exp_work->current_keycode, false);
        exp_work->current_keycode = 0;
    }
    exp_work->text_index++;

    if (exp_work->literal_end_index > 0) {
        exp_work->state = EXPANSION_STATE_TYPE_LITERAL_CHAR;
    } else {
        exp_work->state = EXPANSION_STATE_TYPE_CHAR_START;
    }
    k_work_reschedule(&exp_work->work, K_MSEC(TYPING_DELAY / 2));
}

static void handle_finish(struct expansion_work *exp_work) {
    LOG_INF("Expansion finished successfully.");
    clear_shift_if_active(exp_work);
    if (exp_work->trigger_keycode_to_replay > 0) {
        exp_work->state = EXPANSION_STATE_REPLAY_KEY_PRESS;
        k_work_reschedule(&exp_work->work, K_MSEC(TYPING_DELAY / 2));
    } else {
        exp_work->state = EXPANSION_STATE_IDLE;
    }
}

static void handle_replay_key_press(struct expansion_work *exp_work) {
    LOG_DBG("Replaying trigger key press: 0x%04X", exp_work->trigger_keycode_to_replay);
    send_and_flush_key_action(exp_work->trigger_keycode_to_replay, true);
    exp_work->state = EXPANSION_STATE_REPLAY_KEY_RELEASE;
    k_work_reschedule(&exp_work->work, K_MSEC(TYPING_DELAY / 2));
}

static void handle_replay_key_release(struct expansion_work *exp_work) {
    LOG_DBG("Releasing trigger key: 0x%04X", exp_work->trigger_keycode_to_replay);
    send_and_flush_key_action(exp_work->trigger_keycode_to_replay, false);
    exp_work->state = EXPANSION_STATE_IDLE;
}

static void win_unicode_handler(struct expansion_work *exp_work) {
    switch(exp_work->state) {
        case EXPANSION_STATE_UNICODE_START:
            LOG_DBG("WINDOWS: Starting Unicode typing for U+%04X", exp_work->unicode_codepoint);
            snprintf(exp_work->unicode_hex_buffer, sizeof(exp_work->unicode_hex_buffer), "%u", exp_work->unicode_codepoint);
            exp_work->unicode_hex_index = 0;
            exp_work->state = EXPANSION_STATE_UNICODE_PRESS_MODS;
            k_work_reschedule(&exp_work->work, K_NO_WAIT);
            break;
        case EXPANSION_STATE_UNICODE_PRESS_MODS:
            LOG_DBG("WINDOWS: Pressing LALT.");
            zmk_hid_register_mods(MOD_LALT);
            zmk_endpoints_send_report(HID_USAGE_KEY);
            exp_work->state = EXPANSION_STATE_UNICODE_TYPE_HEX;
            k_work_reschedule(&exp_work->work, K_MSEC(TYPING_DELAY));
            break;
        case EXPANSION_STATE_UNICODE_TYPE_HEX:
            if (exp_work->current_keycode != 0) {
                send_and_flush_key_action(exp_work->current_keycode, false);
                exp_work->current_keycode = 0;
                exp_work->unicode_hex_index++;
            }
            char digit = exp_work->unicode_hex_buffer[exp_work->unicode_hex_index];
            if (digit == '\0') {
                exp_work->state = EXPANSION_STATE_UNICODE_RELEASE_MODS;
            } else {
                exp_work->current_keycode = get_numpad_keycode(digit);
                LOG_DBG("WINDOWS: Typing numpad digit '%c'", digit);
                send_and_flush_key_action(exp_work->current_keycode, true);
            }
            k_work_reschedule(&exp_work->work, K_MSEC(TYPING_DELAY));
            break;
        case EXPANSION_STATE_UNICODE_RELEASE_MODS:
            LOG_DBG("WINDOWS: Releasing LALT.");
            zmk_hid_unregister_mods(MOD_LALT);
            zmk_endpoints_send_report(HID_USAGE_KEY);
            exp_work->state = EXPANSION_STATE_TYPE_CHAR_START;
            k_work_reschedule(&exp_work->work, K_MSEC(TYPING_DELAY));
            break;
        default: break;
    }
}

static void macos_unicode_handler(struct expansion_work *exp_work) {
    switch(exp_work->state) {
        case EXPANSION_STATE_UNICODE_START:
            LOG_DBG("MACOS: Starting Unicode typing for U+%04X", exp_work->unicode_codepoint);
            snprintf(exp_work->unicode_hex_buffer, sizeof(exp_work->unicode_hex_buffer), "%04x", exp_work->unicode_codepoint);
            exp_work->unicode_hex_index = 0;
            exp_work->state = EXPANSION_STATE_UNICODE_PRESS_MODS;
            k_work_reschedule(&exp_work->work, K_NO_WAIT);
            break;
        case EXPANSION_STATE_UNICODE_PRESS_MODS:
            LOG_DBG("MACOS: Pressing LALT (Option).");
            zmk_hid_register_mods(MOD_LALT);
            zmk_endpoints_send_report(HID_USAGE_KEY);
            exp_work->state = EXPANSION_STATE_UNICODE_TYPE_HEX;
            k_work_reschedule(&exp_work->work, K_MSEC(TYPING_DELAY));
            break;
        case EXPANSION_STATE_UNICODE_TYPE_HEX:
            if (exp_work->current_keycode != 0) {
                send_and_flush_key_action(exp_work->current_keycode, false);
                exp_work->current_keycode = 0;
                exp_work->unicode_hex_index++;
            }
            char hex_digit = exp_work->unicode_hex_buffer[exp_work->unicode_hex_index];
            if (hex_digit == '\0') {
                exp_work->state = EXPANSION_STATE_UNICODE_RELEASE_MODS;
            } else {
                exp_work->current_keycode = get_hex_keycode(hex_digit);
                LOG_DBG("MACOS: Typing hex digit '%c'", hex_digit);
                send_and_flush_key_action(exp_work->current_keycode, true);
            }
            k_work_reschedule(&exp_work->work, K_MSEC(TYPING_DELAY));
            break;
        case EXPANSION_STATE_UNICODE_RELEASE_MODS:
            LOG_DBG("MACOS: Releasing LALT (Option).");
            zmk_hid_unregister_mods(MOD_LALT);
            zmk_endpoints_send_report(HID_USAGE_KEY);
            exp_work->state = EXPANSION_STATE_TYPE_CHAR_START;
            k_work_reschedule(&exp_work->work, K_MSEC(TYPING_DELAY));
            break;
        default: break;
    }
}

static void linux_unicode_handler(struct expansion_work *exp_work) {
    switch(exp_work->state) {
        case EXPANSION_STATE_UNICODE_START:
            LOG_DBG("LINUX: Starting Unicode typing for U+%04X", exp_work->unicode_codepoint);
            snprintf(exp_work->unicode_hex_buffer, sizeof(exp_work->unicode_hex_buffer), "%x", exp_work->unicode_codepoint);
            exp_work->unicode_hex_index = 0;
            exp_work->state = EXPANSION_STATE_UNICODE_PRESS_MODS;
            k_work_reschedule(&exp_work->work, K_NO_WAIT);
            break;
        case EXPANSION_STATE_UNICODE_PRESS_MODS:
            LOG_DBG("LINUX: Pressing LCTL|LSFT.");
            zmk_hid_register_mods(MOD_LCTL | MOD_LSFT);
            zmk_endpoints_send_report(HID_USAGE_KEY);
            exp_work->state = EXPANSION_STATE_UNICODE_PRESS_KEY;
            k_work_reschedule(&exp_work->work, K_MSEC(TYPING_DELAY));
            break;
        case EXPANSION_STATE_UNICODE_PRESS_KEY:
            LOG_DBG("LINUX: Pressing U.");
            send_and_flush_key_action(HID_USAGE_KEY_KEYBOARD_U, true);
            exp_work->state = EXPANSION_STATE_UNICODE_RELEASE_KEY;
            k_work_reschedule(&exp_work->work, K_MSEC(TYPING_DELAY));
            break;
        case EXPANSION_STATE_UNICODE_RELEASE_KEY:
            LOG_DBG("LINUX: Releasing U.");
            send_and_flush_key_action(HID_USAGE_KEY_KEYBOARD_U, false);
            exp_work->state = EXPANSION_STATE_UNICODE_RELEASE_MODS;
            k_work_reschedule(&exp_work->work, K_MSEC(TYPING_DELAY));
            break;
        case EXPANSION_STATE_UNICODE_RELEASE_MODS:
             LOG_DBG("LINUX: Releasing LCTL|LSFT.");
             zmk_hid_unregister_mods(MOD_LCTL | MOD_LSFT);
             zmk_endpoints_send_report(HID_USAGE_KEY);
             exp_work->state = EXPANSION_STATE_UNICODE_TYPE_HEX;
             k_work_reschedule(&exp_work->work, K_MSEC(TYPING_DELAY));
             break;
        case EXPANSION_STATE_UNICODE_TYPE_HEX:
             if (exp_work->current_keycode != 0) {
                send_and_flush_key_action(exp_work->current_keycode, false);
                exp_work->current_keycode = 0;
                exp_work->unicode_hex_index++;
            }
            char hex_digit = exp_work->unicode_hex_buffer[exp_work->unicode_hex_index];
            if (hex_digit == '\0') {
                exp_work->state = EXPANSION_STATE_UNICODE_PRESS_TERMINATOR;
            } else {
                exp_work->current_keycode = get_hex_keycode(hex_digit);
                LOG_DBG("LINUX: Typing hex digit '%c'", hex_digit);
                send_and_flush_key_action(exp_work->current_keycode, true);
            }
            k_work_reschedule(&exp_work->work, K_MSEC(TYPING_DELAY));
            break;
        case EXPANSION_STATE_UNICODE_PRESS_TERMINATOR:
            LOG_DBG("LINUX: Pressing Enter.");
            send_and_flush_key_action(HID_USAGE_KEY_KEYBOARD_RETURN_ENTER, true);
            exp_work->state = EXPANSION_STATE_UNICODE_RELEASE_TERMINATOR;
            k_work_reschedule(&exp_work->work, K_MSEC(TYPING_DELAY));
            break;
        case EXPANSION_STATE_UNICODE_RELEASE_TERMINATOR:
            LOG_DBG("LINUX: Releasing Enter.");
            send_and_flush_key_action(HID_USAGE_KEY_KEYBOARD_RETURN_ENTER, false);
            exp_work->state = EXPANSION_STATE_TYPE_CHAR_START;
            k_work_reschedule(&exp_work->work, K_MSEC(TYPING_DELAY));
            break;
        default: break;
    }
}

static void handle_unicode_dispatch(struct expansion_work *exp_work) {
    LOG_DBG("Dispatching to OS-specific unicode handler. Mode: %d", expander_data.unicode_mode);
    switch (expander_data.unicode_mode) {
        case TE_UNICODE_MODE_WINDOWS:
            win_unicode_handler(exp_work);
            break;
        case TE_UNICODE_MODE_MACOS:
            macos_unicode_handler(exp_work);
            break;
        case TE_UNICODE_MODE_LINUX:
            linux_unicode_handler(exp_work);
            break;
    }
}

static uint32_t get_numpad_keycode(char digit) {
    switch (digit) {
        case '0': return HID_USAGE_KEY_KEYPAD_0_AND_INSERT;
        case '1': return HID_USAGE_KEY_KEYPAD_1_AND_END;
        case '2': return HID_USAGE_KEY_KEYPAD_2_AND_DOWN_ARROW;
        case '3': return HID_USAGE_KEY_KEYPAD_3_AND_PAGEDN;
        case '4': return HID_USAGE_KEY_KEYPAD_4_AND_LEFT_ARROW;
        case '5': return HID_USAGE_KEY_KEYPAD_5;
        case '6': return HID_USAGE_KEY_KEYPAD_6_AND_RIGHT_ARROW;
        case '7': return HID_USAGE_KEY_KEYPAD_7_AND_HOME;
        case '8': return HID_USAGE_KEY_KEYPAD_8_AND_UP_ARROW;
        case '9': return HID_USAGE_KEY_KEYPAD_9_AND_PAGEUP;
        default:  return 0;
    }
}

static uint32_t get_hex_keycode(char hex_digit) {
    char d = tolower(hex_digit);
    if (d >= '0' && d <= '9') {
        if (d == '0') return HID_USAGE_KEY_KEYBOARD_0_AND_RIGHT_PARENTHESIS;
        return HID_USAGE_KEY_KEYBOARD_1_AND_EXCLAMATION + (d - '1');
    }
    if (d >= 'a' && d <= 'f') {
        return HID_USAGE_KEY_KEYBOARD_A + (d - 'a');
    }
    return 0;
}

int start_expansion(struct expansion_work *work_item, const char *expanded_text, uint8_t len_to_delete, uint16_t trigger_keycode) {
    LOG_INF("Starting expansion: text='%s', backspaces=%d, replay_keycode=0x%04X", expanded_text, len_to_delete, trigger_keycode);
    cancel_current_expansion(work_item);

    work_item->expanded_text = expanded_text;
    work_item->trigger_keycode_to_replay = trigger_keycode;
    work_item->backspace_count = len_to_delete;
    work_item->text_index = 0;
    work_item->literal_end_index = 0;
    work_item->start_time_ms = k_uptime_get();
    work_item->shift_mod_active = false;
    work_item->current_keycode = 0;

    if (work_item->backspace_count > 0) {
        work_item->state = EXPANSION_STATE_START_BACKSPACE;
    } else {
        work_item->state = EXPANSION_STATE_START_TYPING;
    }

    LOG_DBG("Scheduling expansion work, initial state: %d", work_item->state);
    k_work_reschedule(&work_item->work, K_MSEC(10));
    return 0;
}
