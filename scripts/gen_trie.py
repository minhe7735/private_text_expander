import sys
import re
from pathlib import Path

try:
    from devicetree import dtlib
except ImportError:
    print("Error: The 'dtlib' library is required but could not be imported.", file=sys.stderr)
    print("       This may be an issue with the PYTHONPATH environment for the build command.", file=sys.stderr)
    sys.exit(1)


# Using a constant for the null/invalid index for clarity.
TRIE_NULL_CHILD = 65535


class TrieNode:
    def __init__(self):
        self.children = {}
        self.is_terminal = False
        self.expanded_text = None

def build_trie(expansions):
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

def char_to_trie_index(c):
    """
    Maps a character to its corresponding index in the trie's children array.
    NOTE: This logic is intentionally mirrored in the firmware at src/trie.c.
          Any changes to the character set or mapping must be synchronized there.
    """
    if 'a' <= c <= 'z':
        return ord(c) - ord('a')
    if '0' <= c <= '9':
        return 26 + (ord(c) - ord('0'))
    return -1

def parse_dts_with_dtlib(dts_path_str):
    """
    Parses the given DTS file to find and extract text expansion definitions.
    This version is refactored to avoid code duplication.
    """
    expansions = {}
    try:
        dt = dtlib.DT(dts_path_str)

        def process_expander_node(expander_node):
            """Helper to process a node that is a text_expander behavior."""
            for child in expander_node.nodes.values():
                if "short_code" in child.props and "expanded_text" in child.props:
                    short_code = child.props["short_code"].to_string()
                    expanded_text = child.props["expanded_text"].to_string()
                    expansions[short_code] = expanded_text
        
        for node in dt.node_iter():
            if "compatible" not in node.props:
                continue

            compatible_prop = node.props["compatible"]
            
            # Unify handling for properties that are a single string or a list of strings.
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


def generate_c_source(expansions):
    if not expansions:
        print("Warning: No text expansion definitions found in the keymap.")
        return """
#include <zephyr/kernel.h>
#include <zmk/trie.h>
const struct trie_node *zmk_text_expander_trie_root = NULL;
const char *zmk_text_expander_get_string(uint16_t offset) { return NULL; }
"""

    root = build_trie(expansions)
    nodes = []
    string_pool = ""
    node_map = {id(root): 0}
    
    q = [root]
    while q:
        node = q.pop(0)
        nodes.append(node)
        for child in node.children.values():
            if id(child) not in node_map:
                node_map[id(child)] = len(node_map)
                q.append(child)

    print("Expansions found:")
    for short, expanded in expansions.items():
        print(f"  '{short}' -> '{expanded}'")

    c_nodes = []
    for i, node in enumerate(nodes):
        children_indices = [TRIE_NULL_CHILD] * 36
        for char, child_node in node.children.items():
            idx = char_to_trie_index(char)
            if idx != -1:
                children_indices[idx] = node_map[id(child_node)]

        text_offset = TRIE_NULL_CHILD
        if node.expanded_text:
            text_offset = len(string_pool)
            
            string_pool += node.expanded_text + '\0'
            
            print(f"Node {i}: text='{repr(node.expanded_text)}' -> offset={text_offset}")

        c_nodes.append({
            "children": children_indices,
            "is_terminal": 1 if node.is_terminal else 0,
            "expanded_text_offset": text_offset,
        })

    escaped_string_pool = escape_for_c_string(string_pool)

    c_file_content = """
#include <zephyr/kernel.h>
#include <zmk/trie.h>

static const char zmk_text_expander_string_pool[] = "{string_pool}";

static const struct trie_node zmk_text_expander_generated_trie[] = {{
""".format(string_pool=escaped_string_pool)

    for i, n in enumerate(c_nodes):
        children_str = ", ".join(map(str, n["children"]))
        c_file_content += f"    {{ .children = {{ {children_str} }}, .is_terminal = {n['is_terminal']}, .expanded_text_offset = {n['expanded_text_offset']} }},\n"

    c_file_content += "};\n\n"
    c_file_content += "const struct trie_node *zmk_text_expander_trie_root = &zmk_text_expander_generated_trie[0];\n"
    c_file_content += "const char *zmk_text_expander_get_string(uint16_t offset) {\n"
    c_file_content += "    if (offset >= sizeof(zmk_text_expander_string_pool)) return NULL;\n"
    c_file_content += "    return &zmk_text_expander_string_pool[offset];\n"
    c_file_content += "}\n"

    return c_file_content

def escape_for_c_string(text):
    """
    Properly escape a string for use in a C string literal.
    """
    escape_map = {
        '\\': '\\\\',  # Backslash
        '"': '\\"',    # Double quote
        '\'': '\\\'',  # Single quote (apostrophe)
        '\n': '\\n',   # Newline
        '\t': '\\t',   # Tab
        '\r': '\\r',   # Carriage return
        '\b': '\\b',   # Backspace
        '\f': '\\f',   # Form feed
        '\v': '\\v',   # Vertical tab
        '\0': '\\0',   # Null character
        '`': '\\`',    # Backtick
    }
    
    result = []
    for char in text:
        if char in escape_map:
            result.append(escape_map[char])
        elif ord(char) < 32 or ord(char) > 126:
            # Non-printable ASCII characters - use octal escape
            result.append(f'\\{ord(char):03o}')
        else:
            result.append(char)
    
    return ''.join(result)

if __name__ == "__main__":
    if len(sys.argv) < 4:
        print("Usage: python gen_trie.py <build_dir> <output_c_file> <output_h_file>")
        sys.exit(1)
    
    build_dir = sys.argv[1]
    output_c_path = sys.argv[2]
    output_h_path = sys.argv[3]
    
    build_path = Path(build_dir)
    dts_files = list(build_path.rglob('zephyr.dts')) # Use rglob to find the file

    if not dts_files:
        print(f"Error: Processed devicetree source (zephyr.dts) not found in '{build_dir}'", file=sys.stderr)
        # Create empty files on error to not break the build
        Path(output_c_path).touch()
        Path(output_h_path).touch()
        sys.exit(1)

    dts_path = dts_files[0]
    print(f"Found devicetree source at: {dts_path}")
    
    expansions = parse_dts_with_dtlib(str(dts_path))
    
    # --- Generate the C source file ---
    c_code = generate_c_source(expansions)
    with open(output_c_path, 'w') as f:
        f.write(c_code)

    # --- Generate the H header file ---
    longest_short_code_len = 0
    if expansions:
        longest_short_code_len = len(max(expansions.keys(), key=len))
    
    h_file_content = f"""
/*
 * Copyright (c) 2024 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

// Automatically generated by gen_trie.py
// The length of the longest short code defined in the user's keymap.
#define ZMK_TEXT_EXPANDER_GENERATED_MAX_SHORT_LEN {longest_short_code_len}
"""
    with open(output_h_path, 'w') as f:
        f.write(h_file_content)
