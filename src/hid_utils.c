#include <zmk/hid_utils.h>
#include <zmk/endpoints.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(hid_utils, LOG_LEVEL_DBG);

int send_and_flush_key_action(uint32_t keycode, bool pressed) {
    LOG_DBG("Sending key action: keycode=0x%04X, pressed=%s", keycode, pressed ? "true" : "false");
    int ret = send_key_action(keycode, pressed);
    if (ret < 0) {
        LOG_ERR("Failed to send key action: %d", ret);
        return ret;
    }
    
    LOG_DBG("Flushing HID report for usage page 0x%02X", HID_USAGE_KEY);
    return zmk_endpoints_send_report(HID_USAGE_KEY);
}

uint32_t char_to_keycode(char c, bool *needs_shift) {
    *needs_shift = false;
    LOG_DBG("Converting char '%c' (ASCII: %d) to keycode", c, c);
    uint32_t keycode = 0;

#if !defined(CONFIG_ZMK_TEXT_EXPANDER_ULTRA_LOW_MEMORY)
    static const keycode_map_entry_t keycode_lut[KEYCODE_LUT_SIZE] __attribute__((section(".rodata"))) = {
        [' ' - KEYCODE_LUT_OFFSET] = {HID_USAGE_KEY_KEYBOARD_SPACEBAR, 0},
        ['!' - KEYCODE_LUT_OFFSET] = {HID_USAGE_KEY_KEYBOARD_1_AND_EXCLAMATION, 1},
        ['"' - KEYCODE_LUT_OFFSET] = {HID_USAGE_KEY_KEYBOARD_APOSTROPHE_AND_QUOTE, 1},
        ['#' - KEYCODE_LUT_OFFSET] = {HID_USAGE_KEY_KEYBOARD_3_AND_HASH, 1},
        ['$' - KEYCODE_LUT_OFFSET] = {HID_USAGE_KEY_KEYBOARD_4_AND_DOLLAR, 1},
        ['%' - KEYCODE_LUT_OFFSET] = {HID_USAGE_KEY_KEYBOARD_5_AND_PERCENT, 1},
        ['&' - KEYCODE_LUT_OFFSET] = {HID_USAGE_KEY_KEYBOARD_7_AND_AMPERSAND, 1},
        ['\'' - KEYCODE_LUT_OFFSET] = {HID_USAGE_KEY_KEYBOARD_APOSTROPHE_AND_QUOTE, 0},
        ['(' - KEYCODE_LUT_OFFSET] = {HID_USAGE_KEY_KEYBOARD_9_AND_LEFT_PARENTHESIS, 1},
        [')' - KEYCODE_LUT_OFFSET] = {HID_USAGE_KEY_KEYBOARD_0_AND_RIGHT_PARENTHESIS, 1},
        ['*' - KEYCODE_LUT_OFFSET] = {HID_USAGE_KEY_KEYBOARD_8_AND_ASTERISK, 1},
        ['+' - KEYCODE_LUT_OFFSET] = {HID_USAGE_KEY_KEYBOARD_EQUAL_AND_PLUS, 1},
        [',' - KEYCODE_LUT_OFFSET] = {HID_USAGE_KEY_KEYBOARD_COMMA_AND_LESS_THAN, 0},
        ['-' - KEYCODE_LUT_OFFSET] = {HID_USAGE_KEY_KEYBOARD_MINUS_AND_UNDERSCORE, 0},
        ['.' - KEYCODE_LUT_OFFSET] = {HID_USAGE_KEY_KEYBOARD_PERIOD_AND_GREATER_THAN, 0},
        ['/' - KEYCODE_LUT_OFFSET] = {HID_USAGE_KEY_KEYBOARD_SLASH_AND_QUESTION_MARK, 0},
        ['0' - KEYCODE_LUT_OFFSET] = {HID_USAGE_KEY_KEYBOARD_0_AND_RIGHT_PARENTHESIS, 0},
        ['1' - KEYCODE_LUT_OFFSET] = {HID_USAGE_KEY_KEYBOARD_1_AND_EXCLAMATION, 0},
        ['2' - KEYCODE_LUT_OFFSET] = {HID_USAGE_KEY_KEYBOARD_2_AND_AT, 0},
        ['3' - KEYCODE_LUT_OFFSET] = {HID_USAGE_KEY_KEYBOARD_3_AND_HASH, 0},
        ['4' - KEYCODE_LUT_OFFSET] = {HID_USAGE_KEY_KEYBOARD_4_AND_DOLLAR, 0},
        ['5' - KEYCODE_LUT_OFFSET] = {HID_USAGE_KEY_KEYBOARD_5_AND_PERCENT, 0},
        ['6' - KEYCODE_LUT_OFFSET] = {HID_USAGE_KEY_KEYBOARD_6_AND_CARET, 0},
        ['7' - KEYCODE_LUT_OFFSET] = {HID_USAGE_KEY_KEYBOARD_7_AND_AMPERSAND, 0},
        ['8' - KEYCODE_LUT_OFFSET] = {HID_USAGE_KEY_KEYBOARD_8_AND_ASTERISK, 0},
        ['9' - KEYCODE_LUT_OFFSET] = {HID_USAGE_KEY_KEYBOARD_9_AND_LEFT_PARENTHESIS, 0},
        [':' - KEYCODE_LUT_OFFSET] = {HID_USAGE_KEY_KEYBOARD_SEMICOLON_AND_COLON, 1},
        [';' - KEYCODE_LUT_OFFSET] = {HID_USAGE_KEY_KEYBOARD_SEMICOLON_AND_COLON, 0},
        ['<' - KEYCODE_LUT_OFFSET] = {HID_USAGE_KEY_KEYBOARD_COMMA_AND_LESS_THAN, 1},
        ['=' - KEYCODE_LUT_OFFSET] = {HID_USAGE_KEY_KEYBOARD_EQUAL_AND_PLUS, 0},
        ['>' - KEYCODE_LUT_OFFSET] = {HID_USAGE_KEY_KEYBOARD_PERIOD_AND_GREATER_THAN, 1},
        ['?' - KEYCODE_LUT_OFFSET] = {HID_USAGE_KEY_KEYBOARD_SLASH_AND_QUESTION_MARK, 1},
        ['@' - KEYCODE_LUT_OFFSET] = {HID_USAGE_KEY_KEYBOARD_2_AND_AT, 1},
        ['[' - KEYCODE_LUT_OFFSET] = {HID_USAGE_KEY_KEYBOARD_LEFT_BRACKET_AND_LEFT_BRACE, 0},
        ['\\' - KEYCODE_LUT_OFFSET] = {HID_USAGE_KEY_KEYBOARD_BACKSLASH_AND_PIPE, 0},
        [']' - KEYCODE_LUT_OFFSET] = {HID_USAGE_KEY_KEYBOARD_RIGHT_BRACKET_AND_RIGHT_BRACE, 0},
        ['^' - KEYCODE_LUT_OFFSET] = {HID_USAGE_KEY_KEYBOARD_6_AND_CARET, 1},
        ['_' - KEYCODE_LUT_OFFSET] = {HID_USAGE_KEY_KEYBOARD_MINUS_AND_UNDERSCORE, 1},
        ['`' - KEYCODE_LUT_OFFSET] = {HID_USAGE_KEY_KEYBOARD_GRAVE_ACCENT_AND_TILDE, 0},
        ['{' - KEYCODE_LUT_OFFSET] = {HID_USAGE_KEY_KEYBOARD_LEFT_BRACKET_AND_LEFT_BRACE, 1},
        ['|' - KEYCODE_LUT_OFFSET] = {HID_USAGE_KEY_KEYBOARD_BACKSLASH_AND_PIPE, 1},
        ['}' - KEYCODE_LUT_OFFSET] = {HID_USAGE_KEY_KEYBOARD_RIGHT_BRACKET_AND_RIGHT_BRACE, 1},
        ['~' - KEYCODE_LUT_OFFSET] = {HID_USAGE_KEY_KEYBOARD_GRAVE_ACCENT_AND_TILDE, 1},
        ['a' - KEYCODE_LUT_OFFSET ... 'z' - KEYCODE_LUT_OFFSET] = {HID_USAGE_KEY_KEYBOARD_A, 0},
        ['A' - KEYCODE_LUT_OFFSET ... 'Z' - KEYCODE_LUT_OFFSET] = {HID_USAGE_KEY_KEYBOARD_A, 1},
    };

    if (c >= 'a' && c <= 'z') {
        *needs_shift = keycode_lut[c - KEYCODE_LUT_OFFSET].needs_shift;
        keycode = keycode_lut[c - KEYCODE_LUT_OFFSET].keycode + (c - 'a');
    } else if (c >= 'A' && c <= 'Z') {
        *needs_shift = keycode_lut[c - KEYCODE_LUT_OFFSET].needs_shift;
        keycode = keycode_lut[c - KEYCODE_LUT_OFFSET].keycode + (c - 'A');
    } else if (c == '\n') { keycode = HID_USAGE_KEY_KEYBOARD_RETURN_ENTER; }
    else if (c == '\t') { keycode = HID_USAGE_KEY_KEYBOARD_TAB; }
    else if (c == '\b') { keycode = HID_USAGE_KEY_KEYBOARD_DELETE_BACKSPACE; }
    else if (c < KEYCODE_LUT_OFFSET || c >= (KEYCODE_LUT_OFFSET + KEYCODE_LUT_SIZE)) {
        LOG_WRN("Character '%c' out of lookup table range", c);
        return 0;
    } else {
        const keycode_map_entry_t *entry = &keycode_lut[c - KEYCODE_LUT_OFFSET];
        *needs_shift = entry->needs_shift;
        keycode = entry->keycode;
    }
#else
    if (c >= 'a' && c <= 'z') { keycode = HID_USAGE_KEY_KEYBOARD_A + (c - 'a'); }
    else if (c >= 'A' && c <= 'Z') { *needs_shift = true; keycode = HID_USAGE_KEY_KEYBOARD_A + (c - 'A'); }
    else if (c >= '1' && c <= '9') { keycode = HID_USAGE_KEY_KEYBOARD_1_AND_EXCLAMATION + (c - '1'); }
    else if (c == '0') { keycode = HID_USAGE_KEY_KEYBOARD_0_AND_RIGHT_PARENTHESIS; }
    else switch (c) {
        case '\n': keycode = HID_USAGE_KEY_KEYBOARD_RETURN_ENTER; break;
        case '\t': keycode = HID_USAGE_KEY_KEYBOARD_TAB; break;
        case ' ':  keycode = HID_USAGE_KEY_KEYBOARD_SPACEBAR; break;
        case '\b': keycode = HID_USAGE_KEY_KEYBOARD_DELETE_BACKSPACE; break;
        case '!': *needs_shift = true; keycode = HID_USAGE_KEY_KEYBOARD_1_AND_EXCLAMATION; break;
        case '@': *needs_shift = true; keycode = HID_USAGE_KEY_KEYBOARD_2_AND_AT; break;
        case '#': *needs_shift = true; keycode = HID_USAGE_KEY_KEYBOARD_3_AND_HASH; break;
        case '$': *needs_shift = true; keycode = HID_USAGE_KEY_KEYBOARD_4_AND_DOLLAR; break;
        case '%': *needs_shift = true; keycode = HID_USAGE_KEY_KEYBOARD_5_AND_PERCENT; break;
        case '^': *needs_shift = true; keycode = HID_USAGE_KEY_KEYBOARD_6_AND_CARET; break;
        case '&': *needs_shift = true; keycode = HID_USAGE_KEY_KEYBOARD_7_AND_AMPERSAND; break;
        case '*': *needs_shift = true; keycode = HID_USAGE_KEY_KEYBOARD_8_AND_ASTERISK; break;
        case '(': *needs_shift = true; keycode = HID_USAGE_KEY_KEYBOARD_9_AND_LEFT_PARENTHESIS; break;
        case ')': *needs_shift = true; keycode = HID_USAGE_KEY_KEYBOARD_0_AND_RIGHT_PARENTHESIS; break;
        case '-': keycode = HID_USAGE_KEY_KEYBOARD_MINUS_AND_UNDERSCORE; break;
        case '_': *needs_shift = true; keycode = HID_USAGE_KEY_KEYBOARD_MINUS_AND_UNDERSCORE; break;
        case '=': keycode = HID_USAGE_KEY_KEYBOARD_EQUAL_AND_PLUS; break;
        case '+': *needs_shift = true; keycode = HID_USAGE_KEY_KEYBOARD_EQUAL_AND_PLUS; break;
        case '[': keycode = HID_USAGE_KEY_KEYBOARD_LEFT_BRACKET_AND_LEFT_BRACE; break;
        case '{': *needs_shift = true; keycode = HID_USAGE_KEY_KEYBOARD_LEFT_BRACKET_AND_LEFT_BRACE; break;
        case ']': keycode = HID_USAGE_KEY_KEYBOARD_RIGHT_BRACKET_AND_RIGHT_BRACE; break;
        case '}': *needs_shift = true; keycode = HID_USAGE_KEY_KEYBOARD_RIGHT_BRACKET_AND_RIGHT_BRACE; break;
        case '\\': keycode = HID_USAGE_KEY_KEYBOARD_BACKSLASH_AND_PIPE; break;
        case '|': *needs_shift = true; keycode = HID_USAGE_KEY_KEYBOARD_BACKSLASH_AND_PIPE; break;
        case ';': keycode = HID_USAGE_KEY_KEYBOARD_SEMICOLON_AND_COLON; break;
        case ':': *needs_shift = true; keycode = HID_USAGE_KEY_KEYBOARD_SEMICOLON_AND_COLON; break;
        case '\'': keycode = HID_USAGE_KEY_KEYBOARD_APOSTROPHE_AND_QUOTE; break;
        case '"': *needs_shift = true; keycode = HID_USAGE_KEY_KEYBOARD_APOSTROPHE_AND_QUOTE; break;
        case ',': keycode = HID_USAGE_KEY_KEYBOARD_COMMA_AND_LESS_THAN; break;
        case '<': *needs_shift = true; keycode = HID_USAGE_KEY_KEYBOARD_COMMA_AND_LESS_THAN; break;
        case '.': keycode = HID_USAGE_KEY_KEYBOARD_PERIOD_AND_GREATER_THAN; break;
        case '>': *needs_shift = true; keycode = HID_USAGE_KEY_KEYBOARD_PERIOD_AND_GREATER_THAN; break;
        case '/': keycode = HID_USAGE_KEY_KEYBOARD_SLASH_AND_QUESTION_MARK; break;
        case '?': *needs_shift = true; keycode = HID_USAGE_KEY_KEYBOARD_SLASH_AND_QUESTION_MARK; break;
        case '`': keycode = HID_USAGE_KEY_KEYBOARD_GRAVE_ACCENT_AND_TILDE; break;
        case '~': *needs_shift = true; keycode = HID_USAGE_KEY_KEYBOARD_GRAVE_ACCENT_AND_TILDE; break;
        default:
            LOG_WRN("Unsupported character '%c' in low memory mode", c);
            return 0;
    }
#endif
    LOG_DBG("Converted '%c' to keycode 0x%04X with shift %s", c, keycode, *needs_shift ? "true" : "false");
    return keycode;
}
