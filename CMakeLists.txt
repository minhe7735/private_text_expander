if ((NOT CONFIG_ZMK_SPLIT) OR CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
  if(CONFIG_ZMK_TEXT_EXPANDER)
    
    set(GENERATED_TRIE_C ${CMAKE_CURRENT_BINARY_DIR}/generated_trie.c)
    set(GENERATED_TRIE_H ${CMAKE_CURRENT_BINARY_DIR}/generated_trie.h)

    add_custom_command(
      OUTPUT ${GENERATED_TRIE_C} ${GENERATED_TRIE_H}
      COMMAND
        env "PYTHONPATH=${ZEPHYR_BASE}/scripts/dts/python-devicetree/src"
        ${PYTHON_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/scripts/gen_trie.py
        ${PROJECT_BINARY_DIR}
        ${GENERATED_TRIE_C}
        ${GENERATED_TRIE_H}
      COMMENT "Generating static trie and config for ZMK Text Expander"
    )

    add_custom_target(
      text_expander_generator
      ALL
      DEPENDS ${GENERATED_TRIE_C} ${GENERATED_TRIE_H}
    )

    add_dependencies(text_expander_generator zephyr_generated_headers)

    zephyr_library()
    
    add_dependencies(${ZEPHYR_CURRENT_LIBRARY} text_expander_generator)

    zephyr_library_sources(
      src/text_expander.c
      src/trie.c
      src/hid_utils.c
      src/expansion_engine.c
      ${GENERATED_TRIE_C}
    )
    
    zephyr_library_include_directories(include ${CMAKE_CURRENT_BINARY_DIR})
  endif()
endif()
