#ifndef ZMK_TRIE_H
#define ZMK_TRIE_H

#include <stdbool.h>
#include <stdint.h>

// Sentinel value for a null/invalid index.
#define NULL_INDEX UINT16_MAX

// An entry in the hash table. Represents one child of a trie node.
struct trie_hash_entry {
    char key;                   // The character for this branch (e.g., 'a', '-', etc.)
    uint16_t child_node_index;  // Index to the child trie_node in the main nodes array.
    uint16_t next_entry_index;  // Index to the next entry in this bucket's chain (for collisions).
};

// Represents the hash table for a single trie node's children.
struct trie_hash_table {
    uint16_t buckets_start_index; // Index into the global hash_buckets array.
    uint8_t num_buckets;          // Number of buckets in this specific hash table.
};

// Represents a node in the static, read-only trie.
struct trie_node {
    uint16_t hash_table_index;      // Index to the hash table for this node's children.
    uint16_t expanded_text_offset;  // Offset to the expanded text in the string pool.
    bool is_terminal;               // Flag indicating if this node represents a complete short code.
    bool preserve_trigger;          // Flag indicating if the trigger key should be replayed.
};

// Extern declarations for the data arrays generated by the Python script.
extern const uint16_t zmk_text_expander_trie_num_nodes;
extern const struct trie_node zmk_text_expander_trie_nodes[];
extern const struct trie_hash_table zmk_text_expander_hash_tables[];
extern const struct trie_hash_entry zmk_text_expander_hash_entries[];
extern const uint16_t zmk_text_expander_hash_buckets[];
extern const char zmk_text_expander_string_pool[];

// Function to get a string from the pool using its offset.
const char *zmk_text_expander_get_string(uint16_t offset);

// Searches for a key and returns the node if it's a terminal.
const struct trie_node *trie_search(const char *key);

// Gets a node for a given key prefix, terminal or not.
const struct trie_node *trie_get_node_for_key(const char *key);

#endif /* ZMK_TRIE_H */
