#include <zephyr/kernel.h>
#include <zmk/hid_utils.h>
#include <zmk/hid.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(hid_utils, LOG_LEVEL_DBG);

int send_and_flush_key_action(uint32_t keycode, bool pressed) {
    LOG_DBG("Sending key action: keycode=%d, pressed=%s", keycode, pressed ? "true" : "false");
    int ret = send_key_action(keycode, pressed);
    if (ret < 0) {
        LOG_ERR("Failed to send key action: %d", ret);
        return ret;
    }
    
    LOG_DBG("Flushing HID report");
    return zmk_endpoints_send_report(HID_USAGE_KEY);
}

uint32_t char_to_keycode(char c, bool *needs_shift) {
    *needs_shift = false;
    LOG_DBG("Converting char '%c' to keycode", c);
    uint32_t keycode = 0;

#if !ZMK_TEXT_EXPANDER_ULTRA_LOW_MEMORY
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
        ['A' - KEYCODE_LUT_OFFSET] = {HID_USAGE_KEY_KEYBOARD_A, 1},
        ['B' - KEYCODE_LUT_OFFSET] = {HID_USAGE_KEY_KEYBOARD_B, 1},
        ['C' - KEYCODE_LUT_OFFSET] = {HID_USAGE_KEY_KEYBOARD_C, 1},
        ['D' - KEYCODE_LUT_OFFSET] = {HID_USAGE_KEY_KEYBOARD_D, 1},
        ['E' - KEYCODE_LUT_OFFSET] = {HID_USAGE_KEY_KEYBOARD_E, 1},
        ['F' - KEYCODE_LUT_OFFSET] = {HID_USAGE_KEY_KEYBOARD_F, 1},
        ['G' - KEYCODE_LUT_OFFSET] = {HID_USAGE_KEY_KEYBOARD_G, 1},
        ['H' - KEYCODE_LUT_OFFSET] = {HID_USAGE_KEY_KEYBOARD_H, 1},
        ['I' - KEYCODE_LUT_OFFSET] = {HID_USAGE_KEY_KEYBOARD_I, 1},
        ['J' - KEYCODE_LUT_OFFSET] = {HID_USAGE_KEY_KEYBOARD_J, 1},
        ['K' - KEYCODE_LUT_OFFSET] = {HID_USAGE_KEY_KEYBOARD_K, 1},
        ['L' - KEYCODE_LUT_OFFSET] = {HID_USAGE_KEY_KEYBOARD_L, 1},
        ['M' - KEYCODE_LUT_OFFSET] = {HID_USAGE_KEY_KEYBOARD_M, 1},
        ['N' - KEYCODE_LUT_OFFSET] = {HID_USAGE_KEY_KEYBOARD_N, 1},
        ['O' - KEYCODE_LUT_OFFSET] = {HID_USAGE_KEY_KEYBOARD_O, 1},
        ['P' - KEYCODE_LUT_OFFSET] = {HID_USAGE_KEY_KEYBOARD_P, 1},
        ['Q' - KEYCODE_LUT_OFFSET] = {HID_USAGE_KEY_KEYBOARD_Q, 1},
        ['R' - KEYCODE_LUT_OFFSET] = {HID_USAGE_KEY_KEYBOARD_R, 1},
        ['S' - KEYCODE_LUT_OFFSET] = {HID_USAGE_KEY_KEYBOARD_S, 1},
        ['T' - KEYCODE_LUT_OFFSET] = {HID_USAGE_KEY_KEYBOARD_T, 1},
        ['U' - KEYCODE_LUT_OFFSET] = {HID_USAGE_KEY_KEYBOARD_U, 1},
        ['V' - KEYCODE_LUT_OFFSET] = {HID_USAGE_KEY_KEYBOARD_V, 1},
        ['W' - KEYCODE_LUT_OFFSET] = {HID_USAGE_KEY_KEYBOARD_W, 1},
        ['X' - KEYCODE_LUT_OFFSET] = {HID_USAGE_KEY_KEYBOARD_X, 1},
        ['Y' - KEYCODE_LUT_OFFSET] = {HID_USAGE_KEY_KEYBOARD_Y, 1},
        ['Z' - KEYCODE_LUT_OFFSET] = {HID_USAGE_KEY_KEYBOARD_Z, 1},
        ['[' - KEYCODE_LUT_OFFSET] = {HID_USAGE_KEY_KEYBOARD_LEFT_BRACKET_AND_LEFT_BRACE, 0},
        ['\\' - KEYCODE_LUT_OFFSET] = {HID_USAGE_KEY_KEYBOARD_BACKSLASH_AND_PIPE, 0},
        [']' - KEYCODE_LUT_OFFSET] = {HID_USAGE_KEY_KEYBOARD_RIGHT_BRACKET_AND_RIGHT_BRACE, 0},
        ['^' - KEYCODE_LUT_OFFSET] = {HID_USAGE_KEY_KEYBOARD_6_AND_CARET, 1},
        ['_' - KEYCODE_LUT_OFFSET] = {HID_USAGE_KEY_KEYBOARD_MINUS_AND_UNDERSCORE, 1},
        ['`' - KEYCODE_LUT_OFFSET] = {HID_USAGE_KEY_KEYBOARD_GRAVE_ACCENT_AND_TILDE, 0},
        ['a' - KEYCODE_LUT_OFFSET] = {HID_USAGE_KEY_KEYBOARD_A, 0},
        ['b' - KEYCODE_LUT_OFFSET] = {HID_USAGE_KEY_KEYBOARD_B, 0},
        ['c' - KEYCODE_LUT_OFFSET] = {HID_USAGE_KEY_KEYBOARD_C, 0},
        ['d' - KEYCODE_LUT_OFFSET] = {HID_USAGE_KEY_KEYBOARD_D, 0},
        ['e' - KEYCODE_LUT_OFFSET] = {HID_USAGE_KEY_KEYBOARD_E, 0},
        ['f' - KEYCODE_LUT_OFFSET] = {HID_USAGE_KEY_KEYBOARD_F, 0},
        ['g' - KEYCODE_LUT_OFFSET] = {HID_USAGE_KEY_KEYBOARD_G, 0},
        ['h' - KEYCODE_LUT_OFFSET] = {HID_USAGE_KEY_KEYBOARD_H, 0},
        ['i' - KEYCODE_LUT_OFFSET] = {HID_USAGE_KEY_KEYBOARD_I, 0},
        ['j' - KEYCODE_LUT_OFFSET] = {HID_USAGE_KEY_KEYBOARD_J, 0},
        ['k' - KEYCODE_LUT_OFFSET] = {HID_USAGE_KEY_KEYBOARD_K, 0},
        ['l' - KEYCODE_LUT_OFFSET] = {HID_USAGE_KEY_KEYBOARD_L, 0},
        ['m' - KEYCODE_LUT_OFFSET] = {HID_USAGE_KEY_KEYBOARD_M, 0},
        ['n' - KEYCODE_LUT_OFFSET] = {HID_USAGE_KEY_KEYBOARD_N, 0},
        ['o' - KEYCODE_LUT_OFFSET] = {HID_USAGE_KEY_KEYBOARD_O, 0},
        ['p' - KEYCODE_LUT_OFFSET] = {HID_USAGE_KEY_KEYBOARD_P, 0},
        ['q' - KEYCODE_LUT_OFFSET] = {HID_USAGE_KEY_KEYBOARD_Q, 0},
        ['r' - KEYCODE_LUT_OFFSET] = {HID_USAGE_KEY_KEYBOARD_R, 0},
        ['s' - KEYCODE_LUT_OFFSET] = {HID_USAGE_KEY_KEYBOARD_S, 0},
        ['t' - KEYCODE_LUT_OFFSET] = {HID_USAGE_KEY_KEYBOARD_T, 0},
        ['u' - KEYCODE_LUT_OFFSET] = {HID_USAGE_KEY_KEYBOARD_U, 0},
        ['v' - KEYCODE_LUT_OFFSET] = {HID_USAGE_KEY_KEYBOARD_V, 0},
        ['w' - KEYCODE_LUT_OFFSET] = {HID_USAGE_KEY_KEYBOARD_W, 0},
        ['x' - KEYCODE_LUT_OFFSET] = {HID_USAGE_KEY_KEYBOARD_X, 0},
        ['y' - KEYCODE_LUT_OFFSET] = {HID_USAGE_KEY_KEYBOARD_Y, 0},
        ['z' - KEYCODE_LUT_OFFSET] = {HID_USAGE_KEY_KEYBOARD_Z, 0},
        ['{' - KEYCODE_LUT_OFFSET] = {HID_USAGE_KEY_KEYBOARD_LEFT_BRACKET_AND_LEFT_BRACE, 1},
        ['|' - KEYCODE_LUT_OFFSET] = {HID_USAGE_KEY_KEYBOARD_BACKSLASH_AND_PIPE, 1},
        ['}' - KEYCODE_LUT_OFFSET] = {HID_USAGE_KEY_KEYBOARD_RIGHT_BRACKET_AND_RIGHT_BRACE, 1},
        ['~' - KEYCODE_LUT_OFFSET] = {HID_USAGE_KEY_KEYBOARD_GRAVE_ACCENT_AND_TILDE, 1},
    };

    if (c == '\n') { keycode = HID_USAGE_KEY_KEYBOARD_RETURN_ENTER; }
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
    else {
        switch (c) {
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
    }
#endif
    LOG_DBG("Converted '%c' to keycode %d with shift %s", c, keycode, *needs_shift ? "true" : "false");
    return keycode;
}
