idf_component_register(
    SRCS
        "my_nvs.cpp"
        "my_nvs_manager.cpp"
    INCLUDE_DIRS
        "include"
    REQUIRES
        "nvs_flash"
)

target_compile_features(${COMPONENT_LIB} PRIVATE cxx_std_20)