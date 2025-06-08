# ZMK Text Expander: Type Less, Say More\!
![zmkiscool](https://github.com/user-attachments/assets/bacdf566-406d-4f88-afa0-e91c9ae1f414)
## What is This?

The ZMK Text Expander is a powerful feature for your ZMK-powered keyboard. It lets you type a short abbreviation (like "eml"), and have it automatically turn into a longer phrase (like "my.long.email.address@example.com"). It's perfect for things you type often!

## Cool Things It Can Do

  * **Your Own Shortcuts:** Create your own personal list of short codes and what they expand into.
  * **Fast & Smart:** Uses a speedy lookup method (a trie) to find your expansions quickly.
  * **Works Smoothly:** Typing out your long text happens in the background, so your keyboard stays responsive.
  * **Custom Typing Feel:** You can adjust how it types, like how it resets if you type something that's not a shortcut.
  * **Easy Setup:** Add your expansions directly in your keyboard's configuration files.

## How to Use It (The Basics)

1.  **Type Your Short Code:** As you type letters (a-z) and numbers (0-9), the text expander remembers them.
      * For example, if you have a shortcut "brb" -> "be right back", you'd type `b`, then `r`, then `b`.
2.  **Trigger the Expansion:** Press a key you've configured to trigger expansions. This can be a dedicated manual trigger key or an automatic trigger key like `Space` or `Enter`.
3.  **Magic!**
      * If the text expander recognizes your short code, its behavior will change depending on how you've defined the expansion. It operates in two modes: **Text Replacement** or **Text Completion**.
      * **Text Replacement Mode:** This is the default behavior. If your `expanded-text` does **not** start with your `short-code`, the module will:
        1.  Automatically "backspace" to delete the short code you typed (and the trigger character).
        2.  Type out the full `expanded-text`.
        3.  Replay the trigger key you pressed (e.g., it will type a `space` if you triggered with the spacebar).
        * *Example:* An expansion like `sig` -> `- Kindly, Me` triggered with the `spacebar` will delete `sig ` and type `- Kindly, Me `.
      * **Text Completion Mode:** This behavior is triggered automatically if your `expanded-text` **does** start with your `short-code`. In this case, the module will:
        1.  Type out only the *rest* of the `expanded-text` immediately after what you typed.
        2.  Replay the trigger key you pressed.
        * *Example:* An expansion like `wip` -> `wip project` triggered with the `spacebar` will keep `wip` on your screen and type ` project ` right after it.
      * If the module doesn't recognize the short code, the trigger key will behave as it normally does.
4.  **Clearing Your Typed Short Code:**
      * Pressing a non-alphanumeric key that is *not* an auto-expand trigger will clear the current short code buffer.
      * `Backspace` will delete the last character you typed into your short code.

## Setting Up Your Expansions

The main way to add your text expansions is through your ZMK keymap file (often ending in `.keymap`).

### Example: Adding Expansions in Your Keymap

```dts
// You must include the ZMK keys header at the top of your .keymap file
#include <dt-bindings/zmk/keys.h>

/ {
    behaviors {
        txt_exp: text_expander {
            compatible = "zmk,behavior-text-expander";

            // --- OPTIONAL TOP-LEVEL SETTINGS ---

            // CORRECT SYNTAX: Use keycode macros like SPACE, ENTER, etc., inside the < >.
            auto-expand-keycodes = <SPACE ENTER TAB>;

            // CORRECT SYNTAX: Use the BSPC macro for backspace.
            undo-keycode = <BSPC>;

            reset-keycodes = <ESC>;
            
            // By default, the trigger key is preserved. To disable this, uncomment the following line.
            // disable-preserve-trigger;


            // --- EXPANSION DEFINITIONS ---
            expansion_email: my_email {
                short-code = "eml";
                expanded-text = "my.personal.email@example.com";
            };

            expansion_signature: my_signature {
                short-code = "sig";
                expanded-text = "- Jane Doe\nSent from my custom keyboard";
            };
        };
    };

    keymap {
        default_layer {
            bindings = <
                &kp A  &kp B  &txt_exp  &kp D
            >;
        };
    };
};
````

**Important:**

  * `short-code`: Keep these to lowercase letters (a-z) and numbers (0-9). Using other characters may lead to unexpected behavior.
  * The `&txt_exp` in your `keymap` should match the name you gave your text expander setup (e.g., `txt_exp` in `&txt_exp` corresponds to `txt_exp: text_expander`).

### Special Characters in Expansions (like Enter or Tab)

Want your expansion to hit "Enter" or "Tab"? You can\!

  * Use `\n` in your `expanded-text` to make it press Enter.
  * Use `\t` for Tab.
  * Use `\"` for a literal double quote (`"`) and `\\` for a literal backslash (`\`).

**Important Note on Special Characters in `expanded-text` (DTS Configuration)**

When defining `expanded-text` in your Device Tree files (e.g., `.keymap`), you might encounter build errors during the CMake configuration stage if you are using certain escape sequences like `\n` (for newline/Enter), `\t` (for Tab), `\"` (for a literal double quote), or `\\` (for a literal backslash). These errors often originate from Zephyr's internal `dts.cmake` script and its handling of string properties containing such characters.

To reliably use these special characters in `expanded-text` defined via Device Tree, your Zephyr environment needs to include fixes for these underlying build system issues. You have the following options:

1.  **Use a Patched Zephyr Tree:** Point your ZMK firmware's Zephyr dependency to the `text-expander` branch of this Zephyr fork: `https://github.com/minhe7735/zephyr/tree/text-expander`. This branch is understood to contain the necessary patches. You would typically adjust your `west.yml` manifest file in your ZMK configuration to point to this Zephyr source.

2.  **Use a ZMK Branch with Patched Zephyr:** Point your ZMK firmware to the `text-expander` branch of this ZMK fork: `https://github.com/minhe7735/zmk/tree/text-expander`. This ZMK branch likely manages its Zephyr dependency to include the required fixes. Again, this would involve updating your `west.yml` manifest.

The underlying Zephyr fixes that address these `dts.cmake` parsing issues correspond to commits `c82799b` and `6edefd8` in the main `zephyrproject-rtos/zephyr` repository. Credit for submitting these commits to the Zephyr project goes to Joel Spadin ([https://github.com/joelspadin](https://github.com/joelspadin)).

Once your Zephyr environment includes these fixes (by using one of the options above or by ensuring your Zephyr version incorporates these commits):

  * To achieve a **newline action (Enter key)** in your expansion, use `\n` in your DTS `expanded-text`.
  * To achieve a **tab action (Tab key)**, use `\t`.
  * To type a **literal double quote (`"`)**, use `\"`.
  * To type a **literal backslash (`\`)**, use `\\`.

## Fine-Tuning (Optional Kconfig Settings)

You can fine-tune the text expander's behavior by adding the following options to your `config/<your_keyboard_name>.conf` file. You must first enable the module with `CONFIG_ZMK_TEXT_EXPANDER=y`.

  * `CONFIG_ZMK_TEXT_EXPANDER_TYPING_DELAY`: The delay in milliseconds between each typed character during expansion (Default: 10).
  * `CONFIG_ZMK_TEXT_EXPANDER_AGGRESSIVE_RESET_MODE`: If on, the current short code is reset immediately if it doesn't match a valid prefix of any stored expansion.
  * `CONFIG_ZMK_TEXT_EXPANDER_RESTART_AFTER_RESET_WITH_TRIGGER_CHAR`: If the short code is reset (e.g., in aggressive mode), the character that caused the reset will start a new short code.
  * `CONFIG_ZMK_TEXT_EXPANDER_NO_DEFAULT_EXPANSION`: If set to `y`, the module will not load the default sample expansion (`exp` -\> `expanded`) if no other expansions are defined.
  * `CONFIG_ZMK_TEXT_EXPANDER_ULTRA_LOW_MEMORY`: A special mode that reduces memory usage by removing the large character-to-keycode lookup table. In this updated version, this mode still supports basic letters, numbers, and a wide range of common special characters (like `!@#$%-=_+[]{}`, etc.), making it a practical choice for memory-constrained devices.

## Getting it into Your ZMK Build

1.  Make sure this text expander module is in your ZMK firmware's build (e.g., in a `modules/behaviors` directory in your ZMK config).
2.  Enable it in your Kconfig file: `CONFIG_ZMK_TEXT_EXPANDER=y`.
