#ifndef ZMK_TRIE_H
#define ZMK_TRIE_H

#include <stdbool.h>
#include <stdint.h>

struct text_expander_data;
struct trie_node;

struct trie_child_link {
    uint8_t index;
    struct trie_node *child_node;
    struct trie_child_link *next_sibling;
};

struct trie_node {
  struct trie_child_link *children;
  char *expanded_text;
  bool is_terminal;
};

struct trie_node *trie_allocate_node(struct text_expander_data *data);

char *trie_allocate_text_storage(struct text_expander_data *data, const char *text_to_store);

struct trie_node *trie_search(struct trie_node *root, const char *key);

int trie_insert(struct trie_node *root, const char *key, const char *value, struct text_expander_data *data);

int char_to_trie_index(char c);

const char *trie_get_expanded_text(struct trie_node *node);

struct trie_node *trie_get_node_for_key(struct trie_node *root, const char *key);

#endif
