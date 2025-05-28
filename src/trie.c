#include <string.h>
#include <errno.h>
#include <zephyr/logging/log.h>

#include <zmk/trie.h>
#include <zmk/text_expander.h>

LOG_MODULE_REGISTER(trie, LOG_LEVEL_DBG);

int char_to_trie_index(char c) {
  if (c >= 'a' && c <= 'z') {
    return c - 'a';
  } else if (c >= '0' && c <= '9') {
    return 26 + (c - '0');
  }
  LOG_WRN("Invalid character for trie index: '%c'", c);
  return -1;
}

struct trie_node *trie_allocate_node(struct text_expander_data *data) {
  if (data->node_pool_used >= ARRAY_SIZE(data->node_pool)) {
    LOG_ERR("Trie node pool is full");
    return NULL;
  }

  struct trie_node *node = &data->node_pool[data->node_pool_used++];
  memset(node, 0, sizeof(struct trie_node));
  LOG_DBG("Allocated trie node, used: %d/%d", data->node_pool_used, ARRAY_SIZE(data->node_pool));
  return node;
}

char *trie_allocate_text_storage(struct text_expander_data *data, const char *text_to_store) {
  uint16_t actual_len = strlen(text_to_store);
  size_t total_len_needed = sizeof(uint16_t) + actual_len + 1;

  if (data->text_pool_used + total_len_needed > sizeof(data->text_pool)) {
    LOG_ERR("Text pool is full");
    return NULL;
  }

  char *base_ptr = &data->text_pool[data->text_pool_used];
  *((uint16_t *)base_ptr) = actual_len;
  char *text_ptr = base_ptr + sizeof(uint16_t);
  strcpy(text_ptr, text_to_store);

  data->text_pool_used += (uint16_t)total_len_needed;
  LOG_DBG("Allocated text storage for '%s', used: %d/%d", text_to_store, data->text_pool_used, sizeof(data->text_pool));
  return text_ptr;
}

uint16_t trie_get_expanded_text_len_from_ptr(const char *expanded_text_ptr) {
    if (!expanded_text_ptr) {
        return 0;
    }
    char* base_ptr = (char*)expanded_text_ptr - sizeof(uint16_t);
    uint16_t len = *((uint16_t*)base_ptr);
    return len;
}

struct trie_node *trie_search(struct trie_node *root, const char *key) {
  if (!root || !key) {
    return NULL;
  }
  LOG_DBG("Searching for key: '%s'", key);

  struct trie_node *current = root;
  for (int i = 0; key[i] != '\0'; i++) {
    char c = key[i];
    int index = char_to_trie_index(c);
    if (index == -1) {
        return NULL;
    }
    if (!current->children[index]) {
        LOG_DBG("Key not found");
        return NULL;
    }
    current = current->children[index];
  }

  LOG_DBG("Search result: %s", current->is_terminal ? "Found" : "Not a terminal node");
  return current->is_terminal ? current : NULL;
}

struct trie_node *trie_get_node_for_key(struct trie_node *root, const char *key) {
  if (!root || !key) {
    return NULL;
  }
  LOG_DBG("Getting node for key: '%s'", key);

  struct trie_node *current = root;
  if (key[0] == '\0') {
    return root;
  }

  for (int i = 0; key[i] != '\0'; i++) {
    char c = key[i];
    int index = char_to_trie_index(c);
    if (index == -1) {
        return NULL;
    }
    if (!current->children[index]) {
        return NULL;
    }
    current = current->children[index];
  }
  return current;
}

const char *trie_get_expanded_text(struct trie_node *node) {
  if (node && node->is_terminal) {
    return node->expanded_text;
  }
  return NULL;
}

int trie_insert(struct trie_node *root, const char *key, const char *value,
                struct text_expander_data *data) {
  if (!root || !key || !value) {
    return -EINVAL;
  }
  LOG_DBG("Inserting key: '%s', value: '%s'", key, value);

  struct trie_node *current = root;

  for (int i = 0; key[i] != '\0'; i++) {
    char c = key[i];
    int index = char_to_trie_index(c);
    if (index == -1) {
      return -EINVAL;
    }
    if (!current->children[index]) {
      current->children[index] = trie_allocate_node(data);
      if (!current->children[index]) {
        return -ENOMEM;
      }
    }
    current = current->children[index];
  }

  if (current->is_terminal && current->expanded_text) {
    LOG_INF("Updating existing entry for key '%s'", key);
  }

  current->expanded_text = trie_allocate_text_storage(data, value);
  if (!current->expanded_text) {
    return -ENOMEM;
  }

  current->is_terminal = true;
  LOG_DBG("Insertion successful");
  return 0;
}
