#include <zephyr/logging/log.h>
#include <zmk/trie.h>
#include <stddef.h>

LOG_MODULE_REGISTER(trie, LOG_LEVEL_DBG);

static const struct trie_node *get_node(uint16_t index) {
    if (index >= zmk_text_expander_trie_num_nodes) {
        LOG_WRN("Node index %u out of bounds.", index);
        return NULL;
    }
    return &zmk_text_expander_trie_nodes[index];
}

const struct trie_node *trie_get_node_for_key(const char *key) {
    LOG_DBG("Searching for key: \"%s\"", key);

    if (!key || zmk_text_expander_trie_num_nodes == 0) {
        LOG_DBG("Key is null or trie is empty, returning NULL.");
        return NULL;
    }

    const struct trie_node *current_node = get_node(0);
    if (!current_node) {
        LOG_ERR("Root node (index 0) is invalid.");
        return NULL;
    }

    for (int i = 0; key[i] != '\0'; i++) {
        char current_char = key[i];
        LOG_DBG("Processing char '%c' at index %d", current_char, i);

        if (current_node->hash_table_index == NULL_INDEX) {
            LOG_DBG("Node has no children (hash_table_index is NULL), stopping search.");
            return NULL;
        }

        const struct trie_hash_table *ht = &zmk_text_expander_hash_tables[current_node->hash_table_index];
        if (ht->num_buckets == 0) {
            LOG_DBG("Node's hash table has zero buckets, stopping search.");
            return NULL;
        }

        uint8_t bucket_index = (uint8_t)current_char % ht->num_buckets;
        LOG_DBG("Hashed '%c' to bucket_index: %u", current_char, bucket_index);

        uint16_t entry_index = zmk_text_expander_hash_buckets[ht->buckets_start_index + bucket_index];

        bool found_child = false;
        while (entry_index != NULL_INDEX) {
            const struct trie_hash_entry *entry = &zmk_text_expander_hash_entries[entry_index];
            LOG_DBG("Checking entry at index %u with key '%c'", entry_index, entry->key);
            if (entry->key == current_char) {
                LOG_DBG("Match found for '%c'. Moving to child node at index %u.", current_char, entry->child_node_index);
                current_node = get_node(entry->child_node_index);
                found_child = true;
                break;
            }
            entry_index = entry->next_entry_index;
        }

        if (!found_child) {
            LOG_DBG("No child found for character '%c'. Key not in trie.", current_char);
            return NULL;
        }
    }

    LOG_DBG("Finished processing key. Returning final node.");
    return current_node;
}

const struct trie_node *trie_search(const char *key) {
    LOG_DBG("trie_search called for key: \"%s\"", key);
    const struct trie_node *node = trie_get_node_for_key(key);
    if (node && node->is_terminal) {
        LOG_DBG("Node found for key and it is a terminal node. Search successful.");
        return node;
    }
    LOG_DBG("Node not found or not a terminal node. Search failed.");
    return NULL;
}
