if(DEFINED CONFIG_ENABLE_SLE_SERVER)
    add_subdirectory_if_exist(SLE_Server)
endif()

if(DEFINED CONFIG_ENABLE_SLE_CLIENT)
    add_subdirectory_if_exist(SLE_Client)
endif()

set(SOURCES "${SOURCES}" PARENT_SCOPE)