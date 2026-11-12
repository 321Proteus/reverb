set(RV_GENERATED_DIR ${CMAKE_BINARY_DIR}/generated)

function(rv_embed_header SRC_FILE VAR_NAME OUT_FILE)
    string(TOUPPER "${VAR_NAME}" CASED_NAME)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${SRC_FILE}")
    file(READ "${SRC_FILE}" CONTENTS)
    # #include lines only serve the IDE: IDA's parser gets the file alone
    string(REGEX REPLACE "(^|\n)[ \t]*#[ \t]*include[^\n]*" "\\1" CONTENTS "${CONTENTS}")
    file(WRITE "${OUT_FILE}" "static const char* ${CASED_NAME} = R\"RVSRC(\n${CONTENTS}\n)RVSRC\";\n")
    message(STATUS "Generated ${VAR_NAME}")
endfunction()

# Embeds ./types.h of the calling directory as RV_XYZ_TYPES_SRC
# in xyz_types_embed.h
function(rv_embed_types NAME)
    rv_embed_header(
        ${CMAKE_CURRENT_SOURCE_DIR}/types.h
        RV_${NAME}_TYPES_SRC
        ${RV_GENERATED_DIR}/${NAME}_types_embed.h
    )
endfunction()

# The IDA SDK silences warnings with -w, which also disables -Werror
# so we need to swap it for an equivalent that doesn't
function(rv_enable_warnings TARGET)
    if (MSVC)
        target_compile_options(${TARGET} PRIVATE /we4715 /we4716)
    else()
        get_target_property(RV_OPTS ${TARGET} COMPILE_OPTIONS)
        list(REMOVE_ITEM RV_OPTS -w)
        set_property(TARGET ${TARGET} PROPERTY COMPILE_OPTIONS ${RV_OPTS})
        if (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
            target_compile_options(${TARGET} PRIVATE -Wno-everything)
        else()
            # IDA's %a format specifier trips -Wformat
            target_compile_options(${TARGET} PRIVATE -Wno-format)
        endif()
        target_compile_options(${TARGET} PRIVATE -Werror=return-type)
    endif()
endfunction()
