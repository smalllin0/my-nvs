file(GLOB SOURCE_CODE
    "src/*.cc"
)


idf_component_register(
    SRCS
        ${SOURCE_CODE}
    INCLUDE_DIRS
        "include"
    REQUIRES
        "nvs_flash"
)

target_compile_features(${COMPONENT_LIB} PRIVATE cxx_std_20)