# Vector map renderer (Mapsforge .map files), a Rust static library from the mapsforge-rs
# repository plus a small C rasterizer. It is built with cargo and the Xtensa Rust toolchain
# (`espup install`, toolchain "esp"). Without that checkout the firmware still builds and keeps
# using the pre-rendered tiles: set MAPSFORGE_RS_DIR in the environment to the checkout, or
# put it next to this repository as IndiaNavi_Firmware.rs/mapsforge-rs.

set(mapsforge_dir "$ENV{MAPSFORGE_RS_DIR}")
if("${mapsforge_dir}" STREQUAL "")
    get_filename_component(mapsforge_dir
        "${CMAKE_CURRENT_LIST_DIR}/../../../IndiaNavi_Firmware.rs/mapsforge-rs" ABSOLUTE)
endif()

if(IDF_TARGET STREQUAL "esp32s3")
    set(mapsforge_triple xtensa-esp32s3-none-elf)
elseif(IDF_TARGET STREQUAL "esp32")
    set(mapsforge_triple xtensa-esp32-none-elf)
endif()

if(EXISTS "${mapsforge_dir}/Cargo.toml" AND mapsforge_triple)
    message(STATUS "Vector maps: building ${mapsforge_dir} for ${mapsforge_triple}")
    idf_component_register(
        SRCS "${mapsforge_dir}/c/mfr_raster.c"
        INCLUDE_DIRS "${mapsforge_dir}/include" "${mapsforge_dir}/c")

    set(mapsforge_target_dir "${CMAKE_BINARY_DIR}/mapsforge-rs")
    set(mapsforge_lib "${mapsforge_target_dir}/${mapsforge_triple}/release/libmapsforge_rs.a")

    # cargo decides if anything has to be rebuilt
    add_custom_target(mapsforge_rs_build
        COMMAND rustup run esp cargo build --release -Zbuild-std=core,alloc
            --target ${mapsforge_triple} --no-default-features --features ffi
            --target-dir "${mapsforge_target_dir}"
        WORKING_DIRECTORY "${mapsforge_dir}"
        BYPRODUCTS "${mapsforge_lib}"
        COMMENT "Building mapsforge-rs"
        VERBATIM)

    add_dependencies(${COMPONENT_LIB} mapsforge_rs_build)
    target_link_libraries(${COMPONENT_LIB} INTERFACE "${mapsforge_lib}")
    # the application sees this and uses vector maps when a map file is on the SD card
    target_compile_definitions(${COMPONENT_LIB} INTERFACE VECTOR_MAP=1)
else()
    message(STATUS "Vector maps: mapsforge-rs not found at ${mapsforge_dir}, using tiles only")
    idf_component_register()
endif()
