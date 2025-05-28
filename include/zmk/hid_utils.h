#ifndef ZMK_HID_UTILS_H
#define ZMK_HID_UTILS_H

#include <stdint.h>
#include <stdbool.h>
#include <zmk/hid.h>
#include <zmk/endpoints.h>

uint32_t char_to_keycode(char c, bool *needs_shift);
int send_and_flush_key_action(uint32_t keycode, bool pressed);

static inline int send_key_action(uint32_t keycode, bool pressed) {
    return pressed ? zmk_hid_keyboard_press(keycode) : zmk_hid_keyboard_release(keycode);
}

#if !ZMK_TEXT_EXPANDER_ULTRA_LOW_MEMORY
#define KEYCODE_LUT_OFFSET 32
#define KEYCODE_LUT_SIZE (127 - KEYCODE_LUT_OFFSET)

typedef struct __attribute__((packed)) {
    uint16_t keycode;
    uint8_t needs_shift : 1;
    uint8_t reserved : 7;
} keycode_map_entry_t;
#endif

#endif
