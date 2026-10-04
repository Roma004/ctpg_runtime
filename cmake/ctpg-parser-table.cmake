include(CMakeParseArguments)

# ctpg_add_parser_table(
#     TARGET        mylang_parser_table        # name of the static library
#     HEADER        path/to/mylang_grammar.hpp # header declaring the table getter
#     TABLE         mylang::parser_table       # fully qualified ctpg::parser_table getter
#     [SYMBOL       mylang_table_data]         # C symbol name; default: <TARGET>_data
#     [NAMESPACE    mylang]                    # C++ namespace; default: empty
#     [LINK_LIBRARIES mylang]                  # libraries to link into the generator
# )
#
# Exports:
#   <TARGET>                       — static library with the table symbol
#   ${work_dir}/<SYMBOL>.bin       — raw blob (for mmap)
#   <TARGET>_gen                   — internal generator executable

function(ctpg_add_parser_table)
    set(opts)
    set(one_value TARGET HEADER TABLE SYMBOL NAMESPACE)
    set(multi_value LINK_LIBRARIES)
    cmake_parse_arguments(CTPG "${opts}" "${one_value}" "${multi_value}" ${ARGN})

    if (NOT CTPG_TARGET)
        message(FATAL_ERROR "ctpg_add_parser_table: TARGET is required")
    endif()

    if (NOT CTPG_HEADER)
        message(FATAL_ERROR "ctpg_add_parser_table: HEADER is required")
    endif()

    if (NOT CTPG_TABLE)
        message(FATAL_ERROR "ctpg_add_parser_table: TABLE is required")
    endif()

    if (NOT CTPG_SYMBOL)
        set(CTPG_SYMBOL "${CTPG_TARGET}_data")
    endif()

    if (NOT CTPG_NAMESPACE)
        set(CTPG_NAMESPACE "")
    endif()

    set(work_dir "${CMAKE_CURRENT_BINARY_DIR}/${CTPG_TARGET}.ctpg")
    file(MAKE_DIRECTORY "${work_dir}")

    # 1. Configure the generator main.
    set(_CTPG_HEADER_HEADER "${CTPG_HEADER}")
    set(_CTPG_TABLE        "${CTPG_TABLE}")
    set(_CTPG_SYMBOL         "${CTPG_SYMBOL}")
    set(_CTPG_NAMESPACE      "${CTPG_NAMESPACE}")

    if (NOT DEFINED CTPG_GENERATOR_TEMPLATE)
        message(FATAL_ERROR "ctpg_add_parser_table: CTPG_GENERATOR_TEMPLATE not set. "
                            "Include ctpg's CMake config first.")
    endif()

    configure_file(
        "${CTPG_GENERATOR_TEMPLATE}"
        "${work_dir}/generator_main.cpp"
        @ONLY)

    # 2. Generator executable.
    add_executable(${CTPG_TARGET}_gen "${work_dir}/generator_main.cpp")
    target_link_libraries(${CTPG_TARGET}_gen PRIVATE
        ctpg::ctpg
        ${CTPG_LINK_LIBRARIES})
    set_target_properties(${CTPG_TARGET}_gen PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${work_dir}/bin")

    # 3. Artifact paths.
    set(bin_file "${work_dir}/${CTPG_SYMBOL}.bin")
    set(cpp_file "${work_dir}/${CTPG_SYMBOL}.cpp")

    # 4. Custom command: run the generator at build time.
    add_custom_command(
        OUTPUT "${bin_file}" "${cpp_file}"
        COMMAND ${CTPG_TARGET}_gen "${bin_file}" "${cpp_file}"
        DEPENDS ${CTPG_TARGET}_gen
        COMMENT "ctpg: generating parse table for ${CTPG_TARGET}"
        VERBATIM)

    # 5. Placeholder so CMake knows about the C++ file before it is generated.
    if (NOT EXISTS "${cpp_file}")
        file(WRITE "${cpp_file}" "// placeholder\n")
    endif()

    # 6. Static library from the generated .cpp.
    add_library(${CTPG_TARGET} STATIC "${cpp_file}")
    set_target_properties(${CTPG_TARGET} PROPERTIES LINKER_LANGUAGE CXX)
    target_link_libraries(${CTPG_TARGET} PUBLIC ctpg::ctpg)

    add_dependencies(${CTPG_TARGET} ${CTPG_TARGET}_gen)
endfunction()
