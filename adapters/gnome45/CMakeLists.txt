include(GNUInstallDirs)

add_executable(nekomote_gnome45 ../layer_shell/main.cpp)
target_compile_definitions(nekomote_gnome45 PRIVATE NEKOMOTE_GNOME45)
target_link_libraries(nekomote_gnome45 PRIVATE nekomote_wayland_wallpaper_host
    nekomote::host_gpu nekomote_warnings)

find_program(GLIB_COMPILE_SCHEMAS glib-compile-schemas REQUIRED)
set(NEKOMOTE_GNOME_UUID "nekomote@nekopara.uk")
set(extension_source "${CMAKE_CURRENT_SOURCE_DIR}/extension")
set(extension_dir "${CMAKE_CURRENT_BINARY_DIR}/${NEKOMOTE_GNOME_UUID}")
set(schema "org.gnome.shell.extensions.nekomote.gschema.xml")
add_custom_command(OUTPUT "${extension_dir}/schemas/gschemas.compiled"
    COMMAND "${CMAKE_COMMAND}" -E copy_directory "${extension_source}" "${extension_dir}"
    COMMAND "${GLIB_COMPILE_SCHEMAS}" --strict "${extension_dir}/schemas"
    DEPENDS "${extension_source}/extension.js" "${extension_source}/metadata.json"
        "${extension_source}/prefs.js" "${extension_source}/schemas/${schema}"
    VERBATIM)
add_custom_target(nekomote_gnome45_extension ALL
    DEPENDS "${extension_dir}/schemas/gschemas.compiled")
install(TARGETS nekomote_gnome45 RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}" COMPONENT gnome45)
install(DIRECTORY "${extension_dir}/"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/gnome-shell/extensions/${NEKOMOTE_GNOME_UUID}"
    COMPONENT gnome45)
