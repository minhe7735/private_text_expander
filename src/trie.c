#include <string.h>
#include <errno.h>
#include <zephyr/logging/log.h>

#include <zmk/trie.h>
#include <zmk/text_expander.h>

LOG_MODULE_REGISTER(trie, LOG_LEVEL_DBG);

#define TRIE_ALPHABET_COUNT 26

int char_to_trie_index(char c) {
    if (c >= 'a' && c <= 'z') {
        return c - 'a';
    } else if (c >= '0' && c <= '9') {
        return TRIE_ALPHABET_COUNT + (c - '0');
    }
    LOG_WRN("Invalid character for trie index: '%c'", c);
    return -1;
}

struct trie_node *trie_allocate_node(struct text_expander_data *data) {
    LOG_DBG("Attempting to allocate trie node");
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
    LOG_DBG("Attempting to allocate text storage for '%s'", text_to_store);
    size_t len = strlen(text_to_store);
    size_t total_len_needed = len + 1;
    if (data->text_pool_used + total_len_needed > sizeof(data->text_pool)) {
        LOG_ERR("Text pool is full");
        return NULL;
    }
    char *text_ptr = &data->text_pool[data->text_pool_used];
    strcpy(text_ptr, text_to_store);
    data->text_pool_used += total_len_needed;
    LOG_DBG("Allocated text storage, used: %d/%d", data->text_pool_used, sizeof(data->text_pool));
    return text_ptr;
}

const char *trie_get_expanded_text(struct trie_node *node) {
    if (node && node->is_terminal) {
        LOG_DBG("Retrieving expanded text: '%s'", node->expanded_text);
        return node->expanded_text;
    }
    LOG_DBG("Node is null or not terminal, no expanded text.");
    return NULL;
}

struct trie_node *trie_search(struct trie_node *root, const char *key) {
    LOG_DBG("Searching for key: '%s'", key);
    struct trie_node *current = trie_get_node_for_key(root, key);

    if (current && current->is_terminal) {
        LOG_DBG("Search found terminal node for key '%s'", key);
        return current;
    }
    LOG_DBG("Search found a node for key '%s', but it is not terminal", key);
    return NULL;
}

#if !defined(CONFIG_ZMK_TEXT_EXPANDER_ULTRA_LOW_MEMORY)

struct trie_node *trie_get_node_for_key(struct trie_node *root, const char *key) {
    LOG_DBG("Getting node for key: '%s'", key);
    if (!root || !key) return NULL;
    struct trie_node *current = root;
    for (int i = 0; key[i] != '\0'; i++) {
        int index = char_to_trie_index(key[i]);
        if (index == -1) {
            LOG_WRN("Invalid character in key: '%c'", key[i]);
            return NULL;
        }
        current = current->children[index];
        if (current == NULL) {
            LOG_DBG("No node found for char '%c' in key '%s'", key[i], key);
            return NULL;
        }
    }
    LOG_DBG("Found node for key '%s'", key);
    return current;
}

int trie_insert(struct trie_node *root, const char *key, const char *value,
                struct text_expander_data *data) {
    LOG_DBG("Inserting key: '%s' with value: '%s'", key, value);
    if (!root || !key || !value) {
        LOG_ERR("Invalid arguments for trie_insert");
        return -EINVAL;
    }
    struct trie_node *current = root;
    for (int i = 0; key[i] != '\0'; i++) {
        int index = char_to_trie_index(key[i]);
        if (index == -1) {
            LOG_ERR("Invalid character in key for insertion: '%c'", key[i]);
            return -EINVAL;
        }
        if (current->children[index] == NULL) {
            LOG_DBG("No child for char '%c', creating new node.", key[i]);
            current->children[index] = trie_allocate_node(data);
            if (current->children[index] == NULL) {
                return -ENOMEM;
            }
        }
        current = current->children[index];
    }
    if (current->is_terminal && current->expanded_text) {
        LOG_INF("Updating existing entry for key '%s'", key);
    }
    current->expanded_text = trie_allocate_text_storage(data, value);
    if (!current->expanded_text) return -ENOMEM;
    current->is_terminal = true;
    LOG_DBG("Successfully inserted key '%s'", key);
    return 0;
}

#else

static struct trie_child_link *trie_allocate_child_link(struct text_expander_data *data) {
    LOG_DBG("Attempting to allocate trie child link");
    if (data->child_link_pool_used >= ARRAY_SIZE(data->child_link_pool)) {
        LOG_ERR("Trie child link pool is full");
        return NULL;
    }
    struct trie_child_link *link = &data->child_link_pool[data->child_link_pool_used++];
    memset(link, 0, sizeof(struct trie_child_link));
    LOG_DBG("Allocated child link, used: %d/%d", data->child_link_pool_used, ARRAY_SIZE(data->child_link_pool));
    return link;
}

static struct trie_node *find_child(struct trie_node *node, int index) {
    LOG_DBG("Finding child for index %d", index);
    for (struct trie_child_link *link = node->children; link != NULL; link = link->next_sibling) {
        if (link->index == index) {
            LOG_DBG("Found child node for index %d", index);
            return link->child_node;
        }
    }
    LOG_DBG("No child found for index %d", index);
    return NULL;
}

struct trie_node *trie_get_node_for_key(struct trie_node *root, const char *key) {
    LOG_DBG("Getting node for key: '%s'", key);
    if (!root || !key) return NULL;
    struct trie_node *current = root;
    if (key[0] == '\0') return root;
    for (int i = 0; key[i] != '\0'; i++) {
        int index = char_to_trie_index(key[i]);
        if (index == -1) {
            LOG_WRN("Invalid character in key: '%c'", key[i]);
            return NULL;
        }
        current = find_child(current, index);
        if (current == NULL) {
            LOG_DBG("No node found for char '%c' in key '%s'", key[i], key);
            return NULL;
        }
    }
    LOG_DBG("Found node for key '%s'", key);
    return current;
}

int trie_insert(struct trie_node *root, const char *key, const char *value,
                struct text_expander_data *data) {
    LOG_DBG("Inserting key: '%s' with value: '%s'", key, value);
    if (!root || !key || !value) {
        LOG_ERR("Invalid arguments for trie_insert");
        return -EINVAL;
    }
    struct trie_node *current = root;
    for (int i = 0; key[i] != '\0'; i++) {
        int index = char_to_trie_index(key[i]);
        if (index == -1) {
            LOG_ERR("Invalid character in key for insertion: '%c'", key[i]);
            return -EINVAL;
        }
        struct trie_node *next = find_child(current, index);
        if (next == NULL) {
            LOG_DBG("No child for char '%c', creating new node.", key[i]);
            next = trie_allocate_node(data);
            if (next == NULL) return -ENOMEM;
            struct trie_child_link *new_link = trie_allocate_child_link(data);
            if (new_link == NULL) return -ENOMEM;
            new_link->index = index;
            new_link->child_node = next;
            new_link->next_sibling = current->children;
            current->children = new_link;
        }
        current = next;
    }
    if (current->is_terminal && current->expanded_text) {
        LOG_INF("Updating existing entry for key '%s'", key);
    }
    current->expanded_text = trie_allocate_text_storage(data, value);
    if (!current->expanded_text) return -ENOMEM;
    current->is_terminal = true;
    LOG_DBG("Successfully inserted key '%s'", key);
    return 0;
}

#endif
