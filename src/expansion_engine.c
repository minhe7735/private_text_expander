#include <zephyr/kernel.h>
#include <string.h>
#include <zephyr/logging/log.h>
#include <zmk/hid.h>
#include <zmk/endpoints.h>
#include <zmk/expansion_engine.h>
#include <zmk/hid_utils.h>
#include <zmk/text_expander.h>

LOG_MODULE_REGISTER(expansion_engine, LOG_LEVEL_DBG);

static void clear_shift_if_active(struct expansion_work *exp_work) {
    if (exp_work->shift_mod_active) {
        LOG_DBG("Shift is active, clearing it.");
        zmk_hid_unregister_mods(MOD_LSFT);
        zmk_endpoints_send_report(HID_USAGE_KEY);
        exp_work->shift_mod_active = false;
        LOG_DBG("Unregistered shift modifier");
    }
}

void cancel_current_expansion(struct expansion_work *work_item) {
    if (k_work_cancel_delayable(&work_item->work) >= 0) {
        LOG_INF("Cancelling current expansion work.");

        if (work_item->current_keycode > 0) {
            LOG_DBG("Releasing potentially stuck keycode: %d", work_item->current_keycode);
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

    int ret;
    LOG_DBG("Expansion work handler state: %d", exp_work->state);

    switch (exp_work->state) {
    case EXPANSION_STATE_START_BACKSPACE:
        if (exp_work->backspace_count > 0) {
            LOG_DBG("Starting backspace sequence, %d to go.", exp_work->backspace_count);
            exp_work->state = EXPANSION_STATE_BACKSPACE_PRESS;
            k_work_reschedule(&exp_work->work, K_NO_WAIT);
        } else {
            LOG_DBG("No backspaces needed, starting typing.");
            exp_work->state = EXPANSION_STATE_START_TYPING;
            k_work_reschedule(&exp_work->work, K_MSEC(TYPING_DELAY));
        }
        break;

    case EXPANSION_STATE_BACKSPACE_PRESS:
        LOG_DBG("State: BACKSPACE_PRESS, sending backspace press");
        send_and_flush_key_action(HID_USAGE_KEY_KEYBOARD_DELETE_BACKSPACE, true);
        exp_work->state = EXPANSION_STATE_BACKSPACE_RELEASE;
        k_work_reschedule(&exp_work->work, K_MSEC(TYPING_DELAY / 2));
        break;

    case EXPANSION_STATE_BACKSPACE_RELEASE:
        LOG_DBG("State: BACKSPACE_RELEASE, sending backspace release");
        send_and_flush_key_action(HID_USAGE_KEY_KEYBOARD_DELETE_BACKSPACE, false);
        exp_work->backspace_count--;
        LOG_DBG("Backspaces remaining: %d", exp_work->backspace_count);
        exp_work->state = EXPANSION_STATE_START_BACKSPACE;
        k_work_reschedule(&exp_work->work, K_MSEC(TYPING_DELAY / 2));
        break;

    case EXPANSION_STATE_START_TYPING:
        LOG_DBG("State: START_TYPING, beginning to type expanded text.");
    case EXPANSION_STATE_TYPE_CHAR_START:
        if (exp_work->expanded_text[exp_work->text_index] != '\0') {
            char c = exp_work->expanded_text[exp_work->text_index];
            LOG_DBG("State: TYPE_CHAR_START for char '%c' at index %d", c, exp_work->text_index);
            exp_work->current_keycode = char_to_keycode(c, &exp_work->current_char_needs_shift);

            if (exp_work->current_char_needs_shift && !exp_work->shift_mod_active) {
                LOG_DBG("Registering shift for '%c'", c);
                zmk_hid_register_mods(MOD_LSFT);
                exp_work->shift_mod_active = true;
            } else if (!exp_work->current_char_needs_shift && exp_work->shift_mod_active) {
                LOG_DBG("Unregistering shift for '%c'", c);
                zmk_hid_unregister_mods(MOD_LSFT);
                exp_work->shift_mod_active = false;
            }
            
            exp_work->state = EXPANSION_STATE_TYPE_CHAR_KEY_PRESS;
            k_work_reschedule(&exp_work->work, K_MSEC(1));
        } else {
            exp_work->state = EXPANSION_STATE_FINISH;
            k_work_reschedule(&exp_work->work, K_NO_WAIT);
        }
        break;
    
    case EXPANSION_STATE_TYPE_CHAR_KEY_PRESS:
        LOG_DBG("State: TYPE_CHAR_KEY_PRESS for '%c' (keycode: %d)", exp_work->expanded_text[exp_work->text_index], exp_work->current_keycode);
        if (exp_work->current_keycode > 0) {
            ret = send_and_flush_key_action(exp_work->current_keycode, true);
            if (ret < 0) {
                LOG_ERR("Failed to send key press, aborting expansion.");
                clear_shift_if_active(exp_work);
                exp_work->state = EXPANSION_STATE_IDLE;
                break;
            }
        }
        exp_work->state = EXPANSION_STATE_TYPE_CHAR_KEY_RELEASE;
        k_work_reschedule(&exp_work->work, K_MSEC(TYPING_DELAY / 2));
        break;

    case EXPANSION_STATE_TYPE_CHAR_KEY_RELEASE:
        LOG_DBG("State: TYPE_CHAR_KEY_RELEASE for '%c' (keycode: %d)", exp_work->expanded_text[exp_work->text_index], exp_work->current_keycode);
        if (exp_work->current_keycode > 0) {
            send_and_flush_key_action(exp_work->current_keycode, false);
            exp_work->current_keycode = 0;
        }
        exp_work->text_index++;
        exp_work->state = EXPANSION_STATE_TYPE_CHAR_START;
        k_work_reschedule(&exp_work->work, K_MSEC(TYPING_DELAY / 2));
        break;
    
    case EXPANSION_STATE_FINISH:
        LOG_INF("Expansion finished successfully.");
        clear_shift_if_active(exp_work);
        if (exp_work->trigger_keycode_to_replay > 0) {
            exp_work->state = EXPANSION_STATE_REPLAY_KEY_PRESS;
            k_work_reschedule(&exp_work->work, K_MSEC(TYPING_DELAY / 2));
        } else {
            exp_work->state = EXPANSION_STATE_IDLE;
        }
        break;

    case EXPANSION_STATE_REPLAY_KEY_PRESS:
        send_and_flush_key_action(exp_work->trigger_keycode_to_replay, true);
        exp_work->state = EXPANSION_STATE_REPLAY_KEY_RELEASE;
        k_work_reschedule(&exp_work->work, K_MSEC(TYPING_DELAY / 2));
        break;
    
    case EXPANSION_STATE_REPLAY_KEY_RELEASE:
        send_and_flush_key_action(exp_work->trigger_keycode_to_replay, false);
        exp_work->state = EXPANSION_STATE_IDLE;
        break;

    default:
        LOG_DBG("Entering default state, setting to IDLE");
        exp_work->state = EXPANSION_STATE_IDLE;
        break;
    }
}

int start_expansion(struct expansion_work *work_item, const char *short_code, const char *expanded_text, uint8_t len_to_delete, uint16_t trigger_keycode) {
    LOG_INF("Starting expansion: short_code='%s', expanded_text='%s', backspaces=%d, replay_keycode=%d", short_code, expanded_text, len_to_delete, trigger_keycode);
    cancel_current_expansion(work_item);

    work_item->expanded_text = expanded_text;
    work_item->trigger_keycode_to_replay = trigger_keycode;

    work_item->backspace_count = len_to_delete;
    work_item->text_index = 0;
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
