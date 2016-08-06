set(VERSION_SCRIPT "${CMAKE_CURRENT_LIST_DIR}/version.sh")

function(add_version SOURCE)
  add_custom_target(
    COMMAND "${VERSION_SCRIPT}" > "${SOURCE}"
    COMMENT "Generating version file ${SOURCE}"
    BYPRODUCTS "${SOURCE}"
    VERBATIM
    )
endfunction()
