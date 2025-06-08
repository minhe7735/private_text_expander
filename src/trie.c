#include <zephyr/logging/log.h>
#include <zmk/trie.h>
#include <stddef.h>

LOG_MODULE_REGISTER(trie, LOG_LEVEL_DBG);

static const struct trie_node *get_node(uint16_t index) {
    return &zmk_text_expander_trie_nodes[index];
}

const struct trie_node *trie_get_node_for_key(const char *key) {
    if (!key || zmk_text_expander_trie_num_nodes == 0) {
        return NULL;
    }

    const struct trie_node *current_node = get_node(0);

    for (int i = 0; key[i] != '\0'; i++) {
        char current_char = key[i];

        if (current_node->hash_table_index == NULL_INDEX) {
            return NULL;
        }

        const struct trie_hash_table *ht = &zmk_text_expander_hash_tables[current_node->hash_table_index];
        if (ht->num_buckets == 0) {
            return NULL;
        }

        uint8_t bucket_index = (uint8_t)current_char % ht->num_buckets;

        uint16_t entry_index = zmk_text_expander_hash_buckets[ht->buckets_start_index + bucket_index];

        bool found_child = false;
        while (entry_index != NULL_INDEX) {
            const struct trie_hash_entry *entry = &zmk_text_expander_hash_entries[entry_index];
            if (entry->key == current_char) {
                current_node = get_node(entry->child_node_index);
                found_child = true;
                break;
            }
            entry_index = entry->next_entry_index;
        }

        if (!found_child) {
            return NULL;
        }
    }

    return current_node;
}

const struct trie_node *trie_search(const char *key) {
    const struct trie_node *node = trie_get_node_for_key(key);
    if (node && node->is_terminal) {
        return node;
    }
    return NULL;
}
