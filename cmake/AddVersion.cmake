set(VERSION_SCRIPT "${CMAKE_CURRENT_LIST_DIR}/version.sh")

function(add_version SOURCE)
  add_custom_command(
    COMMAND "${VERSION_SCRIPT}" "${CMAKE_CURRENT_SOURCE_DIR}" > "${SOURCE}"
    COMMENT "Generating version file ${SOURCE}"
    OUTPUT "${SOURCE}" .PHONY
    VERBATIM
    )
endfunction()
