# ZMK Text Expander: Type Less, Say More!

## What is This?

The ZMK Text Expander is a cool feature for your ZMK-powered keyboard. It lets you type a short abbreviation (like "eml"), and have it automatically turn into a longer phrase (like "my.long.email.address@example.com"). It's perfect for things you type often!

## Cool Things It Can Do

* **Your Own Shortcuts:** Create your own personal list of short codes and what they expand into.
* **Fast & Smart:** Uses a speedy lookup method (a trie) to find your expansions quickly.
* **Works Smoothly:** Typing out your long text happens in the background, so your keyboard stays responsive.
* **Custom Typing Feel:** You can adjust how it types, like how it resets if you type something that's not a shortcut.
* **Easy Setup:** Add your expansions directly in your keyboard's configuration files.

## How to Use It (The Basics)

1.  **Type Your Short Code:** As you type letters (a-z) and numbers (0-9), the text expander remembers them.
    * For example, if you have a shortcut "brb" -> "be right back", you'd type `b`, then `r`, then `b`.

2.  **Trigger the Expansion:** Press the special key you've assigned for text expansion (we'll cover setting this up below).

3.  **Magic!**
    * If the text expander recognizes your short code:
        * It will automatically "backspace" to delete the short code you typed.
        * Then, it will type out the full text for you!
    * If it doesn't recognize the short code, usually nothing happens, or your typed short code might be cleared.

4.  **Clearing Your Typed Short Code:**
    * Pressing `Spacebar` usually clears what you've typed so far if it wasn't a trigger for an expansion.
    * Typing other keys that aren't letters or numbers (like symbols or Enter, depending on your settings) will also typically clear your current short code.
    * `Backspace` will delete the last character you typed into your short code.

## Setting Up Your Expansions

The main way to add your text expansions is through your ZMK keymap file (often ending in `.keymap`).

### Example: Adding Expansions in Your Keymap

Here's how you might add a couple of expansions:

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
````

**Important:**

  * `short_code`: Keep these to lowercase letters (a-z) and numbers (0-9).
  * The `&txt_exp` in your `keymap` should match the name you gave your text expander setup (e.g., `txt_exp` in `&txt_exp` corresponds to `txt_exp: text_expander`).

### Special Characters in Expansions (like Enter or Tab)

Want your expansion to hit "Enter" or "Tab"? You can\!

  * Use `\n` in your `expanded_text` to make it press Enter.
  * Use `\t` for Tab.
  * Use `\"` for a literal double quote (`"`) and `\\` for a literal backslash (`\`).

**Important Note on Special Characters in `expanded_text` (DTS Configuration)**

When defining `expanded_text` in your Device Tree files (e.g., `.keymap`), you might encounter build errors during the CMake configuration stage if you are using certain escape sequences like `\n` (for newline/Enter), `\t` (for Tab), `\"` (for a literal double quote), or `\\` (for a literal backslash). These errors often originate from Zephyr's internal `dts.cmake` script and its handling of string properties containing such characters.

To reliably use these special characters in `expanded_text` defined via Device Tree, your Zephyr environment needs to include fixes for these underlying build system issues. You have the following options:

1.  **Use a Patched Zephyr Tree:** Point your ZMK firmware's Zephyr dependency to the `text-expander` branch of this Zephyr fork: `https://github.com/minhe7735/zephyr/tree/text-expander`. This branch is understood to contain the necessary patches. You would typically adjust your `west.yml` manifest file in your ZMK configuration to point to this Zephyr source.

2.  **Use a ZMK Branch with Patched Zephyr:** Point your ZMK firmware to the `text-expander` branch of this ZMK fork: `https://github.com/minhe7735/zmk/tree/text-expander`. This ZMK branch likely manages its Zephyr dependency to include the required fixes. Again, this would involve updating your `west.yml` manifest.

The underlying Zephyr fixes that address these `dts.cmake` parsing issues correspond to commits `c82799b` and `6edefd8` in the main `zephyrproject-rtos/zephyr` repository. Credit for submitting these commits to the Zephyr project goes to Joel Spadin (https://github.com/joelspadin).

Once your Zephyr environment includes these fixes (by using one of the options above or by ensuring your Zephyr version incorporates these commits):

  * To achieve a **newline action (Enter key)** in your expansion, use `\n` in your DTS `expanded_text`.
  * To achieve a **tab action (Tab key)**, use `\t`.
  * To type a **literal double quote (`"`)**, use `\"`.
  * To type a **literal backslash (`\`)**, use `\\`.

## Fine-Tuning (Optional Kconfig Settings)

For advanced users, there are settings you can tweak in your ZMK configuration files (e.g., `config/<your_keyboard_name>.conf`). These let you control things like:

  * `CONFIG_ZMK_TEXT_EXPANDER_MAX_EXPANSIONS`: How many expansions you can have.
  * `CONFIG_ZMK_TEXT_EXPANDER_MAX_SHORT_LEN`: Max length for your short codes.
  * `CONFIG_ZMK_TEXT_EXPANDER_MAX_EXPANDED_LEN`: Max length for the expanded text.
  * `CONFIG_ZMK_TEXT_EXPANDER_TYPING_DELAY`: How fast the expanded text is typed (milliseconds between characters).
  * `CONFIG_ZMK_TEXT_EXPANDER_AGGRESSIVE_RESET_MODE`: If on, it clears your typed short code faster if it doesn't look like a valid start to any of your shortcuts.
  * `CONFIG_ZMK_TEXT_EXPANDER_RESET_ON_ENTER`/`_RESET_ON_TAB`: Whether pressing Enter or Tab clears the current short code.
  * `CONFIG_ZMK_TEXT_EXPANDER_RESTART_AFTER_RESET_WITH_TRIGGER_CHAR`: If your short code resets (e.g. in aggressive mode), should the character that caused the reset start a new short code?
  * `CONFIG_ZMK_TEXT_EXPANDER_ULTRA_LOW_MEMORY`: Enable this mode to reduce the memory footprint of the text expander at the cost of some features.

You'll need `CONFIG_ZMK_TEXT_EXPANDER=y` to enable the module.

## Getting it into Your ZMK Build

1.  Make sure this text expander module is in your ZMK firmware's build (e.g., in a `modules/behaviors` directory in your ZMK config).
2.  Enable it in your Kconfig file: `CONFIG_ZMK_TEXT_EXPANDER=y`.
