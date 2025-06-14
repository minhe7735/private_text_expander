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

// Forward declarations for state handlers
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

// Unicode state handlers
static void win_start_unicode_typing(struct expansion_work *exp_work);
static void handle_win_uni_press_alt(struct expansion_work *exp_work);
static void handle_win_uni_type_numpad_press(struct expansion_work *exp_work);
static void handle_win_uni_type_numpad_release(struct expansion_work *exp_work);
static void handle_win_uni_release_alt(struct expansion_work *exp_work);

static void macos_start_unicode_typing(struct expansion_work *exp_work);
static void handle_mac_uni_press_option(struct expansion_work *exp_work);
static void handle_mac_uni_type_hex_press(struct expansion_work *exp_work);
static void handle_mac_uni_type_hex_release(struct expansion_work *exp_work);
static void handle_mac_uni_release_option(struct expansion_work *exp_work);

static void linux_start_unicode_typing(struct expansion_work *exp_work);
static void handle_linux_uni_press_ctrl_shift(struct expansion_work *exp_work);
static void handle_linux_uni_press_u(struct expansion_work *exp_work);
static void handle_linux_uni_release_u(struct expansion_work *exp_work);
static void handle_linux_uni_release_ctrl_shift(struct expansion_work *exp_work);
static void handle_linux_uni_type_hex_press(struct expansion_work *exp_work);
static void handle_linux_uni_type_hex_release(struct expansion_work *exp_work);
static void handle_linux_uni_press_terminator(struct expansion_work *exp_work);
static void handle_linux_uni_release_terminator(struct expansion_work *exp_work);


static uint32_t get_numpad_keycode(char digit);
static uint32_t get_hex_keycode(char hex_digit);

// OS Driver Implementations
const struct os_typing_driver win_driver = { .start_unicode_typing = win_start_unicode_typing };
const struct os_typing_driver mac_driver = { .start_unicode_typing = macos_start_unicode_typing };
const struct os_typing_driver linux_driver = { .start_unicode_typing = linux_start_unicode_typing };


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

        case EXPANSION_STATE_UNICODE_START:         expander_data.os_driver->start_unicode_typing(exp_work); break;

        // Windows
        case EXPANSION_STATE_WIN_UNI_PRESS_ALT:          handle_win_uni_press_alt(exp_work); break;
        case EXPANSION_STATE_WIN_UNI_TYPE_NUMPAD_PRESS:  handle_win_uni_type_numpad_press(exp_work); break;
        case EXPANSION_STATE_WIN_UNI_TYPE_NUMPAD_RELEASE:handle_win_uni_type_numpad_release(exp_work); break;
        case EXPANSION_STATE_WIN_UNI_RELEASE_ALT:        handle_win_uni_release_alt(exp_work); break;

        // macOS
        case EXPANSION_STATE_MAC_UNI_PRESS_OPTION:        handle_mac_uni_press_option(exp_work); break;
        case EXPANSION_STATE_MAC_UNI_TYPE_HEX_PRESS:      handle_mac_uni_type_hex_press(exp_work); break;
        case EXPANSION_STATE_MAC_UNI_TYPE_HEX_RELEASE:    handle_mac_uni_type_hex_release(exp_work); break;
        case EXPANSION_STATE_MAC_UNI_RELEASE_OPTION:      handle_mac_uni_release_option(exp_work); break;

        // Linux
        case EXPANSION_STATE_LINUX_UNI_PRESS_CTRL_SHIFT:    handle_linux_uni_press_ctrl_shift(exp_work); break;
        case EXPANSION_STATE_LINUX_UNI_PRESS_U:             handle_linux_uni_press_u(exp_work); break;
        case EXPANSION_STATE_LINUX_UNI_RELEASE_U:           handle_linux_uni_release_u(exp_work); break;
        case EXPANSION_STATE_LINUX_UNI_RELEASE_CTRL_SHIFT:  handle_linux_uni_release_ctrl_shift(exp_work); break;
        case EXPANSION_STATE_LINUX_UNI_TYPE_HEX_PRESS:      handle_linux_uni_type_hex_press(exp_work); break;
        case EXPANSION_STATE_LINUX_UNI_TYPE_HEX_RELEASE:    handle_linux_uni_type_hex_release(exp_work); break;
        case EXPANSION_STATE_LINUX_UNI_PRESS_TERMINATOR:    handle_linux_uni_press_terminator(exp_work); break;
        case EXPANSION_STATE_LINUX_UNI_RELEASE_TERMINATOR:  handle_linux_uni_release_terminator(exp_work); break;

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
        const char *end = strstr(&text[exp_work->text_index + 3], "}}}");
        if (end) {
            exp_work->text_index += 3;
            exp_work->literal_end_index = end - text;
            exp_work->state = EXPANSION_STATE_TYPE_LITERAL_CHAR;
            k_work_reschedule(&exp_work->work, K_NO_WAIT);
            return;
        }
    }

    if (strncmp(&text[exp_work->text_index], "{{", 2) == 0) {
        const char *end = strstr(&text[exp_work->text_index + 2], "}}");
        if (end) {
            size_t cmd_len = end - (text + exp_work->text_index + 2);
            char cmd_buf[16];
            if (cmd_len >= sizeof(cmd_buf)) { cmd_len = sizeof(cmd_buf) - 1; }
            
            strncpy(cmd_buf, &text[exp_work->text_index + 2], cmd_len);
            cmd_buf[cmd_len] = '\0';
            exp_work->text_index += cmd_len + 4;

            LOG_DBG("Parsed command: \"%s\"", cmd_buf);
            
            // --- MODIFIED --- The logic for {{u:XXXX}} is now removed from the C code.
            if (strncmp(cmd_buf, "cmd:", 4) == 0) {
                if (strcmp(&cmd_buf[4], "win") == 0) expander_data.os_driver = &win_driver;
                else if (strcmp(&cmd_buf[4], "mac") == 0) expander_data.os_driver = &mac_driver;
                else if (strcmp(&cmd_buf[4], "linux") == 0) expander_data.os_driver = &linux_driver;
                LOG_INF("Set OS-specific typing driver.");
                exp_work->state = EXPANSION_STATE_TYPE_CHAR_START;
            } else {
                LOG_WRN("Unknown command: %s", cmd_buf);
                exp_work->state = EXPANSION_STATE_TYPE_CHAR_START;
            }
            k_work_reschedule(&exp_work->work, K_MSEC(TYPING_DELAY));
            return;
        }
    }
    
    uint8_t first_byte = text[exp_work->text_index];

    if (first_byte < 0x80) { // Standard ASCII
        exp_work->current_keycode = char_to_keycode(first_byte, &exp_work->current_char_needs_shift);
        exp_work->state = EXPANSION_STATE_TYPE_CHAR_KEY_PRESS;
        k_work_reschedule(&exp_work->work, K_MSEC(1));
        return;
    } else { // Multi-byte UTF-8 sequence
        uint32_t codepoint = 0;
        int utf8_len = 0;

        if ((first_byte & 0xE0) == 0xC0) { // 2-byte
            utf8_len = 2;
            codepoint = (first_byte & 0x1F) << 6;
        } else if ((first_byte & 0xF0) == 0xE0) { // 3-byte
            utf8_len = 3;
            codepoint = (first_byte & 0x0F) << 12;
        } else if ((first_byte & 0xF8) == 0xF0) { // 4-byte
            utf8_len = 4;
            codepoint = (first_byte & 0x07) << 18;
        }

        if (utf8_len > 0 && (exp_work->text_index + utf8_len) <= len) {
            for (int i = 1; i < utf8_len; i++) {
                uint8_t cont_byte = text[exp_work->text_index + i];
                if ((cont_byte & 0xC0) == 0x80) {
                    codepoint |= (cont_byte & 0x3F) << (6 * (utf8_len - 1 - i));
                } else {
                    codepoint = 0; // Invalid sequence
                    break;
                }
            }
            if (codepoint != 0) {
                LOG_DBG("Decoded UTF-8 codepoint: U+%04X", codepoint);
                exp_work->unicode_codepoint = codepoint;
                exp_work->text_index += utf8_len; // Consume all bytes of the char
                exp_work->state = EXPANSION_STATE_UNICODE_START;
                k_work_reschedule(&exp_work->work, K_MSEC(TYPING_DELAY));
                return;
            }
        }
        
        // Invalid or incomplete UTF-8 sequence, skip and continue
        LOG_WRN("Invalid UTF-8 sequence at index %d", exp_work->text_index);
        exp_work->text_index++;
        exp_work->state = EXPANSION_STATE_TYPE_CHAR_START;
        k_work_reschedule(&exp_work->work, K_NO_WAIT);
        return;
    }
}

static void handle_type_literal_char(struct expansion_work *exp_work) {
    if (exp_work->text_index >= exp_work->literal_end_index) {
        exp_work->text_index += 3; // Skip the closing "}}}"
        exp_work->literal_end_index = 0;
        exp_work->state = EXPANSION_STATE_TYPE_CHAR_START;
        k_work_reschedule(&exp_work->work, K_NO_WAIT);
        return;
    }
    
    char char_to_type = exp_work->expanded_text[exp_work->text_index];
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
    
    if (exp_work->current_keycode > 0) {
        send_and_flush_key_action(exp_work->current_keycode, true);
    }
    exp_work->state = EXPANSION_STATE_TYPE_CHAR_KEY_RELEASE;
    k_work_reschedule(&exp_work->work, K_MSEC(TYPING_DELAY / 2));
}

static void handle_type_char_key_release(struct expansion_work *exp_work) {
    if (exp_work->current_keycode > 0) {
        send_and_flush_key_action(exp_work->current_keycode, false);
        exp_work->current_keycode = 0;
    }
    if (exp_work->state != EXPANSION_STATE_UNICODE_START) {
       exp_work->text_index++;
    }

    exp_work->state = (exp_work->literal_end_index > 0) ? EXPANSION_STATE_TYPE_LITERAL_CHAR : EXPANSION_STATE_TYPE_CHAR_START;
    k_work_reschedule(&exp_work->work, K_MSEC(TYPING_DELAY / 2));
}

static void handle_finish(struct expansion_work *exp_work) {
    clear_shift_if_active(exp_work);
    if (exp_work->trigger_keycode_to_replay > 0) {
        exp_work->state = EXPANSION_STATE_REPLAY_KEY_PRESS;
        k_work_reschedule(&exp_work->work, K_MSEC(TYPING_DELAY / 2));
    } else {
        exp_work->state = EXPANSION_STATE_IDLE;
    }
}

static void handle_replay_key_press(struct expansion_work *exp_work) {
    send_and_flush_key_action(exp_work->trigger_keycode_to_replay, true);
    exp_work->state = EXPANSION_STATE_REPLAY_KEY_RELEASE;
    k_work_reschedule(&exp_work->work, K_MSEC(TYPING_DELAY / 2));
}

static void handle_replay_key_release(struct expansion_work *exp_work) {
    send_and_flush_key_action(exp_work->trigger_keycode_to_replay, false);
    exp_work->state = EXPANSION_STATE_IDLE;
}

// Windows Unicode Handlers
static void win_start_unicode_typing(struct expansion_work *exp_work) {
    snprintf(exp_work->unicode_hex_buffer, sizeof(exp_work->unicode_hex_buffer), "%u", exp_work->unicode_codepoint);
    exp_work->unicode_hex_index = 0;
    exp_work->state = EXPANSION_STATE_WIN_UNI_PRESS_ALT;
    k_work_reschedule(&exp_work->work, K_NO_WAIT);
}
static void handle_win_uni_press_alt(struct expansion_work *exp_work) {
    zmk_hid_register_mods(MOD_LALT);
    zmk_endpoints_send_report(HID_USAGE_KEY);
    exp_work->state = EXPANSION_STATE_WIN_UNI_TYPE_NUMPAD_PRESS;
    k_work_reschedule(&exp_work->work, K_MSEC(TYPING_DELAY));
}
static void handle_win_uni_type_numpad_press(struct expansion_work *exp_work) {
    char digit = exp_work->unicode_hex_buffer[exp_work->unicode_hex_index];
    if (digit == '\0') {
        exp_work->state = EXPANSION_STATE_WIN_UNI_RELEASE_ALT;
    } else {
        exp_work->current_keycode = get_numpad_keycode(digit);
        send_and_flush_key_action(exp_work->current_keycode, true);
        exp_work->state = EXPANSION_STATE_WIN_UNI_TYPE_NUMPAD_RELEASE;
    }
    k_work_reschedule(&exp_work->work, K_MSEC(TYPING_DELAY));
}
static void handle_win_uni_type_numpad_release(struct expansion_work *exp_work) {
    send_and_flush_key_action(exp_work->current_keycode, false);
    exp_work->current_keycode = 0;
    exp_work->unicode_hex_index++;
    exp_work->state = EXPANSION_STATE_WIN_UNI_TYPE_NUMPAD_PRESS;
    k_work_reschedule(&exp_work->work, K_MSEC(TYPING_DELAY));
}
static void handle_win_uni_release_alt(struct expansion_work *exp_work) {
    zmk_hid_unregister_mods(MOD_LALT);
    zmk_endpoints_send_report(HID_USAGE_KEY);
    exp_work->state = EXPANSION_STATE_TYPE_CHAR_START;
    k_work_reschedule(&exp_work->work, K_MSEC(TYPING_DELAY));
}

// macOS Unicode Handlers
static void macos_start_unicode_typing(struct expansion_work *exp_work) {
    snprintf(exp_work->unicode_hex_buffer, sizeof(exp_work->unicode_hex_buffer), "%04x", exp_work->unicode_codepoint);
    exp_work->unicode_hex_index = 0;
    exp_work->state = EXPANSION_STATE_MAC_UNI_PRESS_OPTION;
    k_work_reschedule(&exp_work->work, K_NO_WAIT);
}
static void handle_mac_uni_press_option(struct expansion_work *exp_work) {
    zmk_hid_register_mods(MOD_LALT);
    zmk_endpoints_send_report(HID_USAGE_KEY);
    exp_work->state = EXPANSION_STATE_MAC_UNI_TYPE_HEX_PRESS;
    k_work_reschedule(&exp_work->work, K_MSEC(TYPING_DELAY));
}
static void handle_mac_uni_type_hex_press(struct expansion_work *exp_work) {
    char hex_digit = exp_work->unicode_hex_buffer[exp_work->unicode_hex_index];
    if (hex_digit == '\0') {
        exp_work->state = EXPANSION_STATE_MAC_UNI_RELEASE_OPTION;
    } else {
        exp_work->current_keycode = get_hex_keycode(hex_digit);
        send_and_flush_key_action(exp_work->current_keycode, true);
        exp_work->state = EXPANSION_STATE_MAC_UNI_TYPE_HEX_RELEASE;
    }
    k_work_reschedule(&exp_work->work, K_MSEC(TYPING_DELAY));
}
static void handle_mac_uni_type_hex_release(struct expansion_work *exp_work) {
    send_and_flush_key_action(exp_work->current_keycode, false);
    exp_work->current_keycode = 0;
    exp_work->unicode_hex_index++;
    exp_work->state = EXPANSION_STATE_MAC_UNI_TYPE_HEX_PRESS;
    k_work_reschedule(&exp_work->work, K_MSEC(TYPING_DELAY));
}
static void handle_mac_uni_release_option(struct expansion_work *exp_work) {
    zmk_hid_unregister_mods(MOD_LALT);
    zmk_endpoints_send_report(HID_USAGE_KEY);
    exp_work->state = EXPANSION_STATE_TYPE_CHAR_START;
    k_work_reschedule(&exp_work->work, K_MSEC(TYPING_DELAY));
}

// Linux Unicode Handlers
static void linux_start_unicode_typing(struct expansion_work *exp_work) {
    snprintf(exp_work->unicode_hex_buffer, sizeof(exp_work->unicode_hex_buffer), "%x", exp_work->unicode_codepoint);
    exp_work->unicode_hex_index = 0;
    exp_work->state = EXPANSION_STATE_LINUX_UNI_PRESS_CTRL_SHIFT;
    k_work_reschedule(&exp_work->work, K_NO_WAIT);
}
static void handle_linux_uni_press_ctrl_shift(struct expansion_work *exp_work) {
    zmk_hid_register_mods(MOD_LCTL | MOD_LSFT);
    zmk_endpoints_send_report(HID_USAGE_KEY);
    exp_work->state = EXPANSION_STATE_LINUX_UNI_PRESS_U;
    k_work_reschedule(&exp_work->work, K_MSEC(TYPING_DELAY));
}
static void handle_linux_uni_press_u(struct expansion_work *exp_work) {
    send_and_flush_key_action(HID_USAGE_KEY_KEYBOARD_U, true);
    exp_work->state = EXPANSION_STATE_LINUX_UNI_RELEASE_U;
    k_work_reschedule(&exp_work->work, K_MSEC(TYPING_DELAY));
}
static void handle_linux_uni_release_u(struct expansion_work *exp_work) {
    send_and_flush_key_action(HID_USAGE_KEY_KEYBOARD_U, false);
    exp_work->state = EXPANSION_STATE_LINUX_UNI_RELEASE_CTRL_SHIFT;
    k_work_reschedule(&exp_work->work, K_MSEC(TYPING_DELAY));
}
static void handle_linux_uni_release_ctrl_shift(struct expansion_work *exp_work) {
    zmk_hid_unregister_mods(MOD_LCTL | MOD_LSFT);
    zmk_endpoints_send_report(HID_USAGE_KEY);
    exp_work->state = EXPANSION_STATE_LINUX_UNI_TYPE_HEX_PRESS;
    k_work_reschedule(&exp_work->work, K_MSEC(TYPING_DELAY));
}
static void handle_linux_uni_type_hex_press(struct expansion_work *exp_work) {
    char hex_digit = exp_work->unicode_hex_buffer[exp_work->unicode_hex_index];
    if (hex_digit == '\0') {
        exp_work->state = EXPANSION_STATE_LINUX_UNI_PRESS_TERMINATOR;
    } else {
        exp_work->current_keycode = get_hex_keycode(hex_digit);
        send_and_flush_key_action(exp_work->current_keycode, true);
        exp_work->state = EXPANSION_STATE_LINUX_UNI_TYPE_HEX_RELEASE;
    }
    k_work_reschedule(&exp_work->work, K_MSEC(TYPING_DELAY));
}
static void handle_linux_uni_type_hex_release(struct expansion_work *exp_work) {
    send_and_flush_key_action(exp_work->current_keycode, false);
    exp_work->current_keycode = 0;
    exp_work->unicode_hex_index++;
    exp_work->state = EXPANSION_STATE_LINUX_UNI_TYPE_HEX_PRESS;
    k_work_reschedule(&exp_work->work, K_MSEC(TYPING_DELAY));
}
static void handle_linux_uni_press_terminator(struct expansion_work *exp_work) {
    send_and_flush_key_action(HID_USAGE_KEY_KEYBOARD_RETURN_ENTER, true);
    exp_work->state = EXPANSION_STATE_LINUX_UNI_RELEASE_TERMINATOR;
    k_work_reschedule(&exp_work->work, K_MSEC(TYPING_DELAY));
}
static void handle_linux_uni_release_terminator(struct expansion_work *exp_work) {
    send_and_flush_key_action(HID_USAGE_KEY_KEYBOARD_RETURN_ENTER, false);
    exp_work->state = EXPANSION_STATE_TYPE_CHAR_START;
    k_work_reschedule(&exp_work->work, K_MSEC(TYPING_DELAY));
}

// Keycode Helpers
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

    work_item->state = (work_item->backspace_count > 0) ? EXPANSION_STATE_START_BACKSPACE : EXPANSION_STATE_START_TYPING;

    LOG_DBG("Scheduling expansion work, initial state: %d", work_item->state);
    k_work_reschedule(&work_item->work, K_MSEC(10));
    return 0;
}
