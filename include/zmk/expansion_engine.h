#ifndef ZMK_EXPANSION_ENGINE_H
#define ZMK_EXPANSION_ENGINE_H

#include <zephyr/kernel.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

struct expansion_work;

void expansion_work_handler(struct k_work *work);
int start_expansion(struct expansion_work *work_item, const char *short_code, const char *expanded_text, uint8_t short_len);
void cancel_current_expansion(struct expansion_work *work_item);

#endif
