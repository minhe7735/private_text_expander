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

char *trie_allocate_string(struct text_expander_data *data, const char *text_to_store) {
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
    
    if (current) {
        LOG_DBG("Search found a non-terminal node for key '%s'", key);
    } else {
        LOG_DBG("Search did not find any node for key '%s'", key);
    }
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
        LOG_DBG("Traversing with char '%c' at index %d", key[i], index);
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
            LOG_DBG("No child for char '%c' at index %d, creating new node.", key[i], index);
            current->children[index] = trie_allocate_node(data);
            if (current->children[index] == NULL) {
                LOG_ERR("Failed to allocate node for char '%c'", key[i]);
                return -ENOMEM;
            }
        }
        current = current->children[index];
    }
    if (current->is_terminal && current->expanded_text) {
        LOG_WRN("Duplicate key defined in keymap: '%s'", key);
    }
    current->expanded_text = trie_allocate_string(data, value);
    if (!current->expanded_text) {
        LOG_ERR("Failed to allocate string for value '%s'", value);
        return -ENOMEM;
    }
    current->is_terminal = true;
    LOG_DBG("Successfully inserted/updated key '%s'", key);
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

static struct trie_child_link *find_child_link(struct trie_node *node, int index) {
    for (struct trie_child_link *link = node->children; link != NULL; link = link->next_sibling) {
        if (link->index == index) {
            return link;
        }
    }
    return NULL;
}

struct trie_node *trie_get_node_for_key(struct trie_node *root, const char *key) {
    LOG_DBG("Getting node for key: '%s'", key);
    if (!root || !key) return NULL;

    struct trie_node *current_node = root;
    const char *key_ptr = key;

    if (*key_ptr == '\0') return root;

    while (*key_ptr != '\0') {
        int index = char_to_trie_index(*key_ptr);
        if (index == -1) return NULL;

        struct trie_child_link *link = find_child_link(current_node, index);
        if (link == NULL) return NULL;

        struct trie_node *child = link->child_node;
        const char *fragment = child->key_fragment;
        size_t fragment_len = strlen(fragment);

        if (strncmp(key_ptr, fragment, fragment_len) != 0) {
            return NULL;
        }

        key_ptr += fragment_len;
        current_node = child;
    }

    return current_node;
}


int trie_insert(struct trie_node *root, const char *key, const char *value,
                struct text_expander_data *data) {
    LOG_DBG("Inserting key '%s' -> '%s'", key, value);
    if (!root || !key || !value || key[0] == '\0') {
        return -EINVAL;
    }

    struct trie_node *current_node = root;
    const char *key_ptr = key;

    while (true) {
        int index = char_to_trie_index(*key_ptr);
        if (index == -1) return -EINVAL;

        struct trie_child_link *link = find_child_link(current_node, index);

        if (link == NULL) {
            LOG_DBG("Case 1: No matching child. Creating new leaf for '%s'", key_ptr);
            struct trie_node *new_node = trie_allocate_node(data);
            if (new_node == NULL) return -ENOMEM;
            
            new_node->key_fragment = trie_allocate_string(data, key_ptr);
            new_node->expanded_text = trie_allocate_string(data, value);
            new_node->is_terminal = true;
            if (!new_node->key_fragment || !new_node->expanded_text) return -ENOMEM;

            struct trie_child_link *new_link = trie_allocate_child_link(data);
            if (new_link == NULL) return -ENOMEM;
            new_link->index = index;
            new_link->child_node = new_node;
            new_link->next_sibling = current_node->children;
            current_node->children = new_link;
            return 0;
        }

        struct trie_node *child_node = link->child_node;
        const char *fragment = child_node->key_fragment;
        
        size_t common_prefix_len = 0;
        while(key_ptr[common_prefix_len] && fragment[common_prefix_len] && key_ptr[common_prefix_len] == fragment[common_prefix_len]) {
            common_prefix_len++;
        }

        if (common_prefix_len == strlen(fragment)) {
            LOG_DBG("Case 2: Fragment '%s' is prefix of key. Descending.", fragment);
            key_ptr += common_prefix_len;
            current_node = child_node;
            if (*key_ptr == '\0') {
                if (child_node->is_terminal) {
                    LOG_WRN("Duplicate key defined in keymap: '%s'", key);
                }
                child_node->is_terminal = true;
                child_node->expanded_text = trie_allocate_string(data, value);
                if (!child_node->expanded_text) return -ENOMEM;
                return 0;
            }
            continue;
        }

        LOG_DBG("Case 3: Splitting node on fragment '%s'", fragment);
        const char* key_suffix = key_ptr + common_prefix_len;
        const char* fragment_suffix = fragment + common_prefix_len;

        struct trie_node *split_node = trie_allocate_node(data);
        if (split_node == NULL) return -ENOMEM;
        
        char common_prefix_str[common_prefix_len + 1];
        strncpy(common_prefix_str, fragment, common_prefix_len);
        common_prefix_str[common_prefix_len] = '\0';
        split_node->key_fragment = trie_allocate_string(data, common_prefix_str);
        if (!split_node->key_fragment) return -ENOMEM;
        
        child_node->key_fragment = trie_allocate_string(data, fragment_suffix);
        if (!child_node->key_fragment) return -ENOMEM;
        
        struct trie_child_link *child_link_to_split = trie_allocate_child_link(data);
        if (child_link_to_split == NULL) return -ENOMEM;
        child_link_to_split->index = char_to_trie_index(fragment_suffix[0]);
        child_link_to_split->child_node = child_node;
        split_node->children = child_link_to_split;

        if (*key_suffix == '\0') {
            LOG_DBG("New key is a prefix. Marking split node as terminal.");
            split_node->is_terminal = true;
            split_node->expanded_text = trie_allocate_string(data, value);
            if (!split_node->expanded_text) return -ENOMEM;
        } else {
            LOG_DBG("New key has unique suffix '%s'. Creating new leaf.", key_suffix);
            struct trie_node *new_leaf_node = trie_allocate_node(data);
            if (new_leaf_node == NULL) return -ENOMEM;
            new_leaf_node->key_fragment = trie_allocate_string(data, key_suffix);
            new_leaf_node->is_terminal = true;
            new_leaf_node->expanded_text = trie_allocate_string(data, value);
            if (!new_leaf_node->key_fragment || !new_leaf_node->expanded_text) return -ENOMEM;

            struct trie_child_link *new_leaf_link = trie_allocate_child_link(data);
            if (new_leaf_link == NULL) return -ENOMEM;
            new_leaf_link->index = char_to_trie_index(key_suffix[0]);
            new_leaf_link->child_node = new_leaf_node;
            new_leaf_link->next_sibling = split_node->children;
            split_node->children = new_leaf_link;
        }
        
        link->child_node = split_node;
        return 0;
    }
}

#endif
