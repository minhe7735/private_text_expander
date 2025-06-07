#ifndef ZMK_TRIE_H
#define ZMK_TRIE_H

#include <stdbool.h>
#include <stdint.h>

#define TRIE_ALPHABET_LETTERS 26
#define TRIE_ALPHABET_NUMBERS 10
#define TRIE_ALPHABET_SIZE (TRIE_ALPHABET_LETTERS + TRIE_ALPHABET_NUMBERS)

struct text_expander_data;
struct trie_node;

#if !defined(CONFIG_ZMK_TEXT_EXPANDER_ULTRA_LOW_MEMORY)
#define TRIE_CHILDREN_COUNT TRIE_ALPHABET_SIZE
/**
 * @brief Represents a node in the trie. Standard memory implementation.
 */
struct trie_node {
    /** @brief Array of pointers to child nodes, indexed by character. */
    struct trie_node *children[TRIE_CHILDREN_COUNT];
    /** @brief Pointer to the expanded text if this node is a terminal. */
    char *expanded_text;
    /** @brief Flag indicating if this node represents the end of a valid short code. */
    bool is_terminal;
};
#else
/**
 * @brief Linked list element for storing child nodes in low memory mode.
 */
struct trie_child_link {
    /** @brief The character index of the child's key fragment. */
    uint8_t index;
    /** @brief Pointer to the child node. */
    struct trie_node *child_node;
    /** @brief Pointer to the next child of the same parent. */
    struct trie_child_link *next_sibling;
};

/**
 * @brief Represents a node in the Radix Trie. Ultra-low memory implementation.
 */
struct trie_node {
    /** @brief Pointer to a linked list of children. */
    struct trie_child_link *children;
    /** @brief The string fragment that represents the edge leading to this node. */
    char *key_fragment;
    /** @brief Pointer to the expanded text if this node is a terminal. */
    char *expanded_text;
    /** @brief Flag indicating if this node represents the end of a valid short code. */
    bool is_terminal;
};
#endif

struct trie_node *trie_allocate_node(struct text_expander_data *data);
char *trie_allocate_string(struct text_expander_data *data, const char *text_to_store);
struct trie_node *trie_search(struct trie_node *root, const char *key);
int trie_insert(struct trie_node *root, const char *key, const char *value, struct text_expander_data *data);
int char_to_trie_index(char c);
const char *trie_get_expanded_text(struct trie_node *node);
struct trie_node *trie_get_node_for_key(struct trie_node *root, const char *key);

#endif
