file(GLOB SOURCE_CODE
    "./*.cc"
)

idf_component_register(
    SRCS
        test_nvs.cc
    PRIV_REQUIRES 
        my-nvs 
        unity     
)