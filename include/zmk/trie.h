#ifndef ZMK_TRIE_H
#define ZMK_TRIE_H

#include <stdbool.h>
#include <stdint.h>

#define TRIE_ALPHABET_LETTERS 26
#define TRIE_ALPHABET_NUMBERS 10
#define TRIE_ALPHABET_SIZE (TRIE_ALPHABET_LETTERS + TRIE_ALPHABET_NUMBERS)

// Represents a null/invalid child index in the trie node, using the max value for uint16_t.
#define TRIE_NULL_CHILD UINT16_MAX

// Represents a node in the static, read-only trie.
struct trie_node {
    // Array of indices to child nodes in the generated trie array.
    uint16_t children[TRIE_ALPHABET_SIZE];
    // Offset to the expanded text in the generated string pool.
    uint16_t expanded_text_offset;
    // Flag indicating if this node represents the end of a valid short code.
    bool is_terminal;
};

// Global root of the generated trie, defined in the generated C file.
extern const struct trie_node *zmk_text_expander_trie_root;

// Function to get a string from the pool using its offset.
const char *zmk_text_expander_get_string(uint16_t offset);

// Searches for a key and returns the node if it's a terminal.
const struct trie_node *trie_search(const char *key);

// Gets a node for a given key prefix, terminal or not.
const struct trie_node *trie_get_node_for_key(const char *key);

#endif
