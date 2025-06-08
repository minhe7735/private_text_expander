import sys
from pathlib import Path

try:
    from devicetree import dtlib
except ImportError:
    print("Error: The 'dtlib' library is required but could not be imported.", file=sys.stderr)
    print("       This may be an issue with the PYTHONPATH environment for the build command.", file=sys.stderr)
    sys.exit(1)

# Sentinel value for a null/invalid index in the generated C code. Should match UINT16_MAX.
NULL_INDEX = (2**16 - 1)

class TrieNode:
    """Represents a node in the trie during the Python build process."""
    def __init__(self):
        self.children = {}
        self.is_terminal = False
        self.expanded_text = None

def build_trie_from_expansions(expansions):
    """Builds a Python-based trie from the dictionary of expansions."""
    root = TrieNode()
    for short_code, expanded_text in expansions.items():
        node = root
        for char in short_code:
            if char not in node.children:
                node.children[char] = TrieNode()
            node = node.children[char]
        node.is_terminal = True
        node.expanded_text = expanded_text
    return root

def parse_dts_for_expansions(dts_path_str):
    """Parses the given DTS file to find and extract text expansion definitions."""
    expansions = {}
    try:
        dt = dtlib.DT(dts_path_str)

        def process_expander_node(expander_node):
            for child in expander_node.nodes.values():
                if "short_code" in child.props and "expanded_text" in child.props:
                    short_code = child.props["short_code"].to_string()
                    if ' ' in short_code:
                        print(f"Warning: The short code '{short_code}' contains a space, which is a reset character and cannot be used. Skipping this expansion.", file=sys.stderr)
                        continue
                    expanded_text = child.props["expanded_text"].to_string()
                    expansions[short_code] = expanded_text
        
        for node in dt.node_iter():
            if "compatible" not in node.props:
                continue

            compatible_prop = node.props["compatible"]
            compat_strings = []
            if compatible_prop.type == dtlib.Type.STRING:
                compat_strings.append(compatible_prop.to_string())
            elif compatible_prop.type == dtlib.Type.STRINGS:
                compat_strings.extend(compatible_prop.to_strings())
            
            if "zmk,behavior-text-expander" in compat_strings:
                process_expander_node(node)

    except Exception as e:
        print(f"Error parsing DTS file with dtlib: {e}", file=sys.stderr)

    return expansions

def get_next_power_of_2(n):
    """Calculates the next power of 2 for a given number, useful for bucket sizing."""
    if n == 0:
        return 1
    p = 1
    while p < n:
        p <<= 1
    return p

def escape_for_c_string(text):
    """Properly escape a string for use in a C string literal, including null chars."""
    escape_map = {
        '\\': '\\\\', '"': '\\"', '\n': '\\n', '\t': '\\t', '\r': '\\r', '\0': '\\0',
    }
    result = []
    for char in text:
        if char in escape_map:
            result.append(escape_map[char])
        elif ord(char) < 32 or ord(char) > 126:
            result.append(f'\\{ord(char):03o}')
        else:
            result.append(char)
    return ''.join(result)

def generate_static_trie_c_code(expansions):
    """Generates the C source file content for the static trie and hash tables."""
    if not expansions:
        return """
#include <zmk/trie.h>
#include <stddef.h>
const uint16_t zmk_text_expander_trie_num_nodes = 0;
const struct trie_node zmk_text_expander_trie_nodes[] = {};
const struct trie_hash_table zmk_text_expander_hash_tables[] = {};
const struct trie_hash_entry zmk_text_expander_hash_entries[] = {};
const uint16_t zmk_text_expander_hash_buckets[] = {};
const char zmk_text_expander_string_pool[] = "";
const char *zmk_text_expander_get_string(uint16_t offset) { return NULL; }
"""

    root = build_trie_from_expansions(expansions)
    
    string_pool = ""
    c_trie_nodes, c_hash_tables, c_hash_buckets, c_hash_entries = [], [], [], []
    node_q, node_map = [root], {id(root): 0}
    
    while node_q:
        py_node = node_q.pop(0)
        c_trie_nodes.append(py_node)
        for child in py_node.children.values():
            if id(child) not in node_map:
                node_map[id(child)] = len(node_map)
                node_q.append(child)

    for py_node in c_trie_nodes:
        hash_table_index = NULL_INDEX
        if py_node.children:
            hash_table_index = len(c_hash_tables)
            num_children = len(py_node.children)
            num_buckets = get_next_power_of_2(num_children) if num_children > 1 else 1
            buckets_start_index = len(c_hash_buckets)
            buckets = [NULL_INDEX] * num_buckets
            c_hash_tables.append({"buckets_start_index": buckets_start_index, "num_buckets": num_buckets})

            for char, child_py_node in py_node.children.items():
                hash_val = ord(char) % num_buckets
                child_node_index = node_map[id(child_py_node)]
                new_entry_index = len(c_hash_entries)
                next_entry_index = buckets[hash_val]
                c_hash_entries.append({"key": char, "child_node_index": child_node_index, "next_entry_index": next_entry_index})
                buckets[hash_val] = new_entry_index
            c_hash_buckets.extend(buckets)

        expanded_text_offset = NULL_INDEX
        if py_node.is_terminal:
            expanded_text_offset = len(string_pool)
            string_pool += py_node.expanded_text + '\0'
        
        py_node.c_struct_data = {
            "hash_table_index": hash_table_index,
            "expanded_text_offset": expanded_text_offset,
            "is_terminal": 1 if py_node.is_terminal else 0,
        }

    c_parts = ["#include <zmk/trie.h>\n#include <stddef.h> // For NULL\n\n"]
    c_parts.append(f"const uint16_t zmk_text_expander_trie_num_nodes = {len(c_trie_nodes)};\n\n")
    escaped_string_pool = escape_for_c_string(string_pool)
    c_parts.append(f'const char zmk_text_expander_string_pool[] = "{escaped_string_pool}";\n\n')
    
    c_parts.append("const struct trie_node zmk_text_expander_trie_nodes[] = {\n")
    for py_node in c_trie_nodes:
        d = py_node.c_struct_data
        c_parts.append(f"    {{ .hash_table_index = {d['hash_table_index']}, .expanded_text_offset = {d['expanded_text_offset']}, .is_terminal = {d['is_terminal']} }},\n")
    c_parts.append("};\n\n")

    c_parts.append("const struct trie_hash_table zmk_text_expander_hash_tables[] = {\n")
    for ht in c_hash_tables:
        c_parts.append(f"    {{ .buckets_start_index = {ht['buckets_start_index']}, .num_buckets = {ht['num_buckets']} }},\n")
    c_parts.append("};\n\n")
    
    c_parts.append("const uint16_t zmk_text_expander_hash_buckets[] = {\n    " + ", ".join(map(str, c_hash_buckets)) + "\n};\n\n")
    
    c_parts.append("const struct trie_hash_entry zmk_text_expander_hash_entries[] = {\n")
    for entry in c_hash_entries:
        escaped_key = entry['key'].replace('\\', '\\\\').replace("'", "\\'")
        c_parts.append(f"    {{ .key = '{escaped_key}', .child_node_index = {entry['child_node_index']}, .next_entry_index = {entry['next_entry_index']} }},\n")
    c_parts.append("};\n\n")
    
    c_parts.append("const char *zmk_text_expander_get_string(uint16_t offset) {\n")
    c_parts.append("    if (offset >= sizeof(zmk_text_expander_string_pool)) return NULL;\n")
    c_parts.append("    return &zmk_text_expander_string_pool[offset];\n}\n")
    
    return "".join(c_parts)

if __name__ == "__main__":
    if len(sys.argv) < 4:
        print("Usage: python gen_trie.py <build_dir> <output_c_file> <output_h_file>")
        sys.exit(1)
    
    build_dir, output_c_path, output_h_path = sys.argv[1], sys.argv[2], sys.argv[3]
    
    build_path = Path(build_dir)
    dts_files = list(build_path.rglob('zephyr.dts'))

    if not dts_files:
        print(f"Error: Processed devicetree source (zephyr.dts) not found in '{build_dir}'", file=sys.stderr)
        Path(output_c_path).touch()
        Path(output_h_path).touch()
        sys.exit(1)

    dts_path = dts_files[0]
    expansions = parse_dts_for_expansions(str(dts_path))
    
    c_code = generate_static_trie_c_code(expansions)
    with open(output_c_path, 'w') as f:
        f.write(c_code)

    longest_short_len = len(max(expansions.keys(), key=len)) if expansions else 0
    h_file_content = f"""
#pragma once
// Automatically generated file. Do not edit.
#define ZMK_TEXT_EXPANDER_GENERATED_MAX_SHORT_LEN {longest_short_len}
"""
    with open(output_h_path, 'w') as f:
        f.write(h_file_content)
