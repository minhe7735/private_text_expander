#include <zephyr/logging/log.h>
#include <zmk/trie.h>

LOG_MODULE_REGISTER(trie, LOG_LEVEL_DBG);

/**
 * @brief Maps a character to its corresponding index in the trie's children array.
 * @note This logic is intentionally mirrored in the build script at scripts/gen_trie.py.
 * Any changes to the character set or mapping must be synchronized there.
 */
static int char_to_trie_index(char c) {
    if (c >= 'a' && c <= 'z') {
        return c - 'a';
    } else if (c >= '0' && c <= '9') {
        return 26 + (c - '0');
    }
    return -1;
}

const struct trie_node *trie_get_node_for_key(const char *key) {
    if (!key) return NULL;

    const struct trie_node *current = zmk_text_expander_trie_root;
    for (int i = 0; key[i] != '\0'; i++) {
        int index = char_to_trie_index(key[i]);
        if (index == -1) return NULL;

        uint16_t child_index = current->children[index];
        if (child_index == TRIE_NULL_CHILD) {
            return NULL;
        }
        current = &zmk_text_expander_trie_root[child_index];
    }
    return current;
}

const struct trie_node *trie_search(const char *key) {
    const struct trie_node *node = trie_get_node_for_key(key);
    if (node && node->is_terminal) {
        return node;
    }
    return NULL;
}
