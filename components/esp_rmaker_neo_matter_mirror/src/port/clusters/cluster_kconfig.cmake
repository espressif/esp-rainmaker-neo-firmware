# Which esp-matter clusters this port can build, and which the Matter stack needs regardless of the mapping.
#
# The mapping side of this pairing is enforced in C: each src/port/clusters/*.inc is compiled only when the mapping
# needs its cluster, and #errors if the matching CONFIG_SUPPORT_*_CLUSTER is off. This file covers the other direction -
# clusters that are compiled in but that nothing here drives - which is advisory only.

# Clusters with a factory under src/port/clusters/
set(RM_MIRROR_PORT_CLUSTER_CONFIGS
    SUPPORT_IDENTIFY_CLUSTER SUPPORT_GROUPS_CLUSTER SUPPORT_ON_OFF_CLUSTER SUPPORT_LEVEL_CONTROL_CLUSTER
    SUPPORT_FIXED_LABEL_CLUSTER SUPPORT_COLOR_CONTROL_CLUSTER
)

# Required of any commissionable Matter node, or left to the application (the OTA software update cluster is its choice
# of update path).
set(RM_MIRROR_INFRA_CLUSTER_CONFIGS
    SUPPORT_ACCESS_CONTROL_CLUSTER
    SUPPORT_ADMINISTRATOR_COMMISSIONING_CLUSTER
    SUPPORT_BASIC_INFORMATION_CLUSTER
    SUPPORT_BINDING_CLUSTER
    SUPPORT_DESCRIPTOR_CLUSTER
    SUPPORT_GENERAL_COMMISSIONING_CLUSTER
    SUPPORT_GENERAL_DIAGNOSTICS_CLUSTER
    SUPPORT_GROUPCAST_CLUSTER
    SUPPORT_GROUP_KEY_MANAGEMENT_CLUSTER
    SUPPORT_ICD_MANAGEMENT_CLUSTER
    SUPPORT_NETWORK_COMMISSIONING_CLUSTER
    SUPPORT_OPERATIONAL_CREDENTIALS_CLUSTER
    SUPPORT_OTA_SOFTWARE_UPDATE_REQUESTOR_CLUSTER
    SUPPORT_TIME_SYNCHRONIZATION_CLUSTER
    SUPPORT_WIFI_NETWORK_DIAGNOSTICS_CLUSTER
)

# Report enabled CONFIG_SUPPORT_*_CLUSTER options that neither the port builds nor the stack needs.
function (rm_mirror_report_unused_clusters)
    get_cmake_property(_all_vars VARIABLES)
    set(_unused "")
    foreach (_var IN LISTS _all_vars)
        if (NOT _var MATCHES "^CONFIG_(SUPPORT_.*_CLUSTER)$")
            continue()
        endif ()
        set(_name "${CMAKE_MATCH_1}")
        if (NOT "${${_var}}")
            continue()
        endif ()
        if ("${_name}" IN_LIST RM_MIRROR_PORT_CLUSTER_CONFIGS OR "${_name}" IN_LIST RM_MIRROR_INFRA_CLUSTER_CONFIGS)
            continue()
        endif ()
        list(APPEND _unused "${_name}")
    endforeach ()

    if (_unused)
        list(SORT _unused)
        list(LENGTH _unused _count)
        string(REPLACE ";" "\n    " _pretty "${_unused}")
        message(STATUS "Matter mirror: ${_count} Matter cluster(s) are compiled in that the mirror never "
                       "instantiates. Set them to n unless the application drives them itself:\n    ${_pretty}"
        )
    endif ()
endfunction ()
