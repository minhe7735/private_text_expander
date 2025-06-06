# ZMK Text Expander: Type Less, Say More\!
![zmkiscool](https://github.com/user-attachments/assets/bacdf566-406d-4f88-afa0-e91c9ae1f414)
## What is This?

The ZMK Text Expander is a powerful feature for your ZMK-powered keyboard. It lets you type a short abbreviation (like "eml"), and have it automatically turn into a longer phrase (like "my.long.email.address@example.com"). It's perfect for things you type often\!

## Cool Things It Can Do

  * **Your Own Shortcuts:** Create your own personal list of short codes and what they expand into.
  * **Fast & Smart:** Uses a speedy lookup method (a trie) to find your expansions quickly.
  * **Works Smoothly:** Typing out your long text happens in the background, so your keyboard stays responsive.
  * **Custom Typing Feel:** You can adjust how it types, like how it resets if you type something that's not a shortcut.
  * **Easy Setup:** Add your expansions directly in your keyboard's configuration files.

## How to Use It (The Basics)

1.  **Type Your Short Code:** As you type letters (a-z) and numbers (0-9), the text expander remembers them.
      * For example, if you have a shortcut "brb" -\> "be right back", you'd type `b`, then `r`, then `b`.
2.  **Trigger the Expansion:** Press the special key you've assigned for text expansion (we'll cover setting this up below).
3.  **Magic\!**
      * If the text expander recognizes your short code:
          * It will automatically "backspace" to delete the short code you typed.
          * Then, it will type out the full text for you\!
      * If it doesn't recognize the short code, usually nothing happens, or your typed short code might be cleared.
4.  **Clearing Your Typed Short Code:**
      * Pressing `Spacebar` usually clears what you've typed so far if it wasn't a trigger for an expansion.
      * Typing other keys that aren't letters or numbers (like symbols or Enter, depending on your settings) will also typically clear your current short code.
      * `Backspace` will delete the last character you typed into your short code.

## Setting Up Your Expansions

The main way to add your text expansions is through your ZMK keymap file (often ending in `.keymap`).

### Example: Adding Expansions in Your Keymap

```dts
/ {
    behaviors {
        // Give your text expander setup a name, like "txt_exp"
        txt_exp: text_expander {
            compatible = "zmk,behavior-text-expander"; // This line is important!

            // Now, define your shortcuts:
            my_email_shortcut: expansion_email { // Unique name for this specific shortcut
                short_code = "eml";                 // What you'll type
                expanded_text = "my.name@example.com"; // What it becomes
            };

            signature_shortcut: expansion_sig {
                short_code = "sig";
                expanded_text = "- Kindly, Me";
            };
        };
    };

    keymap {
        default_layer { // Or any layer you prefer
            bindings = <
                // ... your other key bindings ...

                // To use the text expander, assign it to a key.
                // For example, replace a key like &kp C with &txt_exp
                &kp A  &kp B  &txt_exp  &kp D

                // ... more key bindings ...
            >;
        };
    };
};
```

**Important:**

  * `short_code`: Keep these to lowercase letters (a-z) and numbers (0-9).
  * The `&txt_exp` in your `keymap` should match the name you gave your text expander setup (e.g., `txt_exp` in `&txt_exp` corresponds to `txt_exp: text_expander`).

### Special Characters in Expansions (like Enter or Tab)

When defining `expanded_text` in your Device Tree files, you can use `\n` for a newline (Enter) and `\t` for a Tab. For literal backslashes or quotes, use `\\` and `\"` respectively.

Note that using these escape sequences in Device Tree Source (`.dts` or `.keymap` files) may require a patched version of Zephyr to build correctly, as older versions had issues parsing these characters.

## Fine-Tuning (Optional Kconfig Settings)

You can fine-tune the text expander's behavior by adding the following options to your `config/<your_keyboard_name>.conf` file. You must first enable the module with `CONFIG_ZMK_TEXT_EXPANDER=y`.

  * `CONFIG_ZMK_TEXT_EXPANDER_MAX_EXPANSIONS`: Sets the total number of unique text expansions that can be stored (Default: 10).
  * `CONFIG_ZMK_TEXT_EXPANDER_MAX_SHORT_LEN`: Sets the maximum number of characters for a short code trigger (Default: 16).
  * `CONFIG_ZMK_TEXT_EXPANDER_MAX_EXPANDED_LEN`: Sets the maximum number of characters for the expanded text output (Default: 256).
  * `CONFIG_ZMK_TEXT_EXPANDER_TYPING_DELAY`: The delay in milliseconds between each typed character during expansion (Default: 10).
  * `CONFIG_ZMK_TEXT_EXPANDER_AGGRESSIVE_RESET_MODE`: If on, the current short code is reset immediately if it doesn't match a valid prefix of any stored expansion.
  * `CONFIG_ZMK_TEXT_EXPANDER_RESTART_AFTER_RESET_WITH_TRIGGER_CHAR`: If the short code is reset (e.g., in aggressive mode), the character that caused the reset will start a new short code.
  * `CONFIG_ZMK_TEXT_EXPANDER_RESET_ON_ENTER`/`_RESET_ON_TAB`: Whether pressing Enter or Tab clears the current short code.
  * `CONFIG_ZMK_TEXT_EXPANDER_NO_DEFAULT_EXPANSION`: If set to `y`, the module will not load the default sample expansion (`exp` -\> `expanded`) if no other expansions are defined.
  * `CONFIG_ZMK_TEXT_EXPANDER_ULTRA_LOW_MEMORY`: A special mode that reduces memory usage by removing the large character-to-keycode lookup table. In this updated version, this mode still supports basic letters, numbers, and a wide range of common special characters (like `!@#$%-=_+[]{}`, etc.), making it a practical choice for memory-constrained devices.

## Getting it into Your ZMK Build

1.  Make sure this text expander module is in your ZMK firmware's build (e.g., in a `modules/behaviors` directory in your ZMK config).
2.  Enable it in your Kconfig file: `CONFIG_ZMK_TEXT_EXPANDER=y`.
