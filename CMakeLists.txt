# CMakeLists.txt for YaPicoprobe
#
# Parameters
# ----------
# - PICO_BOARD                   pico|pico_w|pico_debug_probe, default "pico"
# - PICOPROBE_VERSION_MAJOR      major version number
# - PICOPROBE_VERSION_MINOR      minor version number
# - lot more options below
#
# - GIT_HASH                     short git hash
# - PROJECT                      project name for cmake
#
cmake_minimum_required(VERSION 3.12)

#
# Compile options
#
if(PICO_BOARD STREQUAL "")
    set(PICO_BOARD pico)
endif()
if("${CMAKE_BUILD_TYPE}" STREQUAL "Debug")
    set(DEFAULT_OPT_PROBE_DEBUG_OUT 1)
else()
    set(DEFAULT_OPT_PROBE_DEBUG_OUT 1)
endif()

option(OPT_CMSIS_DAPV1         "Enable CMSIS-DAPv1"                     1)
option(OPT_CMSIS_DAPV2         "Enable CMSIS-DAPv2"                     1)
option(OPT_TARGET_UART         "Enable CDC for target UART I/O"         1)
option(OPT_PROBE_DEBUG_OUT     "Enable CDC for probe debug output"      ${DEFAULT_OPT_PROBE_DEBUG_OUT})
option(OPT_SIGROK              "Enable sigrok"                          0)
option(OPT_MSC                 "Enable Mass Storage Device"             1)
option(OPT_MSC_RAM_UF2         "Enable file 'RAM.UF2' on Mass Storage"  1)
set(OPT_MCU_OVERCLOCK_MHZ      "240" CACHE STRING "Set processor frequency. Should be a multiple of 24MHz, disable with empty string (=120MHz)")
set(OPT_NET                    "NCM" CACHE STRING "Enable lwIP on the Pico via NCM/ECM/RNDIS, disable NET with empty string")
set(OPT_NET_192_168            14 CACHE STRING "Set the subnet of 192.168.x")
option(OPT_NET_ECHO_SERVER     "Enable echo server for testing"         1)
option(OPT_NET_IPERF_SERVER    "Enable iperf server for tuning"         1)
option(OPT_NET_SYSVIEW_SERVER  "Enable SysView over TCPIP"              1)
option(OPT_CDC_SYSVIEW         "Enable SysView over CDC"                0)

# extra
option(OPT_SPECIAL_CLK_FOR_PIO "Ignore clock frequency request of 1MHz (this is for PlatformIO tuning)" 0)


#
# SDK options
#
set(PICO_STDIO_UART 0)             # disable stdio UART, actually this goes to a CDC

#
# below (and above) order is important
#
include(pico_sdk_import.cmake)

set(FREERTOS_KERNEL_PATH ${CMAKE_CURRENT_LIST_DIR}/freertos)
include(FreeRTOS_Kernel_import.cmake)

set(PICO_BOARD_HEADER_DIRS  ${CMAKE_CURRENT_LIST_DIR}/include/boards)
pico_sdk_init()

#    message("--------- " ${PICO_LWIP_PATH})
#    message("--------- " ${PICO_LWIP_CONTRIB_PATH})
#    message("--------- " ${PICO_TINYUSB_PATH})

#
# set some things inherited from Makefile / command line
#
if(PICOPROBE_VERSION_MAJOR)
        add_definitions(-DPICOPROBE_VERSION_MAJOR=${PICOPROBE_VERSION_MAJOR})
else()
        set(PICOPROBE_VERSION_MAJOR 0)
endif()
if(PICOPROBE_VERSION_MINOR)
        add_definitions(-DPICOPROBE_VERSION_MINOR=${PICOPROBE_VERSION_MINOR})
else()
        set(PICOPROBE_VERSION_MINOR 0)
endif()

project(${PROJECT}
        VERSION ${PICOPROBE_VERSION_MAJOR}.${PICOPROBE_VERSION_MINOR}
        DESCRIPTION "YAPicoprobe CMSIS-DAP"
        HOMEPAGE_URL https://github.com/rgrr/yapicoprobe
        LANGUAGES C CXX ASM
)

string(TOUPPER ${PICO_BOARD} PICO_BOARD_UPPER)

#
# set some project specifica
#
add_definitions(
        -DPICO_CORE1_STACK_SIZE=0x400
        -DPICO_HEAP_SIZE=0x0
        -DPICO_STACK_SIZE=0x400
        -DTARGET_BOARD_${PICO_BOARD_UPPER}
        -DOPT_SPECIAL_CLK_FOR_PIO=${OPT_SPECIAL_CLK_FOR_PIO}
)
if(NOT OPT_MCU_OVERCLOCK_MHZ STREQUAL "")
    add_compile_definitions(OPT_MCU_OVERCLOCK_MHZ=${OPT_MCU_OVERCLOCK_MHZ})
endif()

# set version string to "x.yy"
# there are perhaps smarter ways...
if(PICOPROBE_VERSION_MINOR LESS "10")
        add_definitions(-DPICOPROBE_VERSION_STRING="${PICOPROBE_VERSION_MAJOR}.0${PICOPROBE_VERSION_MINOR}")
else()
        add_definitions(-DPICOPROBE_VERSION_STRING="${PICOPROBE_VERSION_MAJOR}.${PICOPROBE_VERSION_MINOR}")
endif()

if(GIT_HASH)
        add_definitions(-DGIT_HASH="${GIT_HASH}")
endif()


########################################################################################################################


add_executable(${PROJECT}
        src/dap_util.c
        src/get_config.c
        src/led.c
        src/main.c
        src/misc_utils.c
        src/probe.c
        src/rtt_console.c
        src/sw_dp_pio.c
        src/sw_lock.c
        src/usb_descriptors.c
)

target_sources(${PROJECT} PRIVATE
        CMSIS_5/CMSIS/DAP/Firmware/Source/DAP.c
        CMSIS_5/CMSIS/DAP/Firmware/Source/JTAG_DP.c
        CMSIS_5/CMSIS/DAP/Firmware/Source/DAP_vendor.c
        CMSIS_5/CMSIS/DAP/Firmware/Source/SWO.c
)

target_include_directories(${PROJECT} PRIVATE
        CMSIS_5/CMSIS/DAP/Firmware/Include/
        CMSIS_5/CMSIS/Core/Include/
        include/
)

#--------------------------------------------------------------------------------------------------
#
# this is for MSC/DAPLink operation
#
target_sources(${PROJECT} PRIVATE
        src/daplink/daplink/util.c
        src/daplink/daplink/drag-n-drop/flash_manager.c
        src/daplink/daplink/interface/swd_host.c
        src/daplink/daplink/interface/target_flash.c
        src/daplink/daplink/settings/settings_rom_stub.c
        src/daplink/family/nordic/target_reset_nrf52.c
        src/daplink/family/nordic/nrf52/target.c
        src/daplink/target/target_board.c
        src/daplink/target/target_family.c
        src/daplink-pico/board/rp2040/pico.c
        src/daplink-pico/board/rp2040/pico_target_utils.c
        src/daplink-pico/board/rp2040/program_flash_generic.c
        src/daplink-pico/family/raspberry/rp2040/target_reset_rp2040.c
)

target_include_directories(${PROJECT} PRIVATE
        src/daplink/daplink
        src/daplink/daplink/cmsis-dap
        src/daplink/daplink/drag-n-drop
        src/daplink/daplink/interface
        src/daplink/daplink/settings
        src/daplink/hic_hal
        src/daplink/target
        src/daplink-pico/board/rp2040
        src/daplink-pico/family/raspberry
        src/daplink-pico/hic_hal/raspberry/rp2040
        $ENV{PICO_SDK_PATH}/src/common/boot_uf2/include
)

# Both are actually dummies to prevent the DAPLink files from throwing errors
# Actually not required because picoprobes structure is different
add_compile_definitions(INTERFACE_RP2040=1
                        DAPLINK_IF=1)

# not sure if this is still required, but this equals to DAPLINK_HIC_ID_RP2040 in daplink.h
add_compile_definitions(DAPLINK_HIC_ID=0x08154711)

# required to get required DAPLink functions
add_compile_definitions(DRAG_N_DROP_SUPPORT=1)

#--------------------------------------------------------------------------------------------------
#
# Enable/disable several USB endpoints
#
if(OPT_TARGET_UART)
    add_compile_definitions(OPT_TARGET_UART=1)
    target_sources(${PROJECT} PRIVATE
        src/cdc/cdc_uart.c
    )
endif()
if(OPT_PROBE_DEBUG_OUT)
    add_compile_definitions(OPT_PROBE_DEBUG_OUT=1)
    target_sources(${PROJECT} PRIVATE
        src/cdc/cdc_debug.c
    )
endif()
if(OPT_CMSIS_DAPV1)
    add_compile_definitions(OPT_CMSIS_DAPV1=1)
endif()
if(OPT_CMSIS_DAPV2)
    add_compile_definitions(OPT_CMSIS_DAPV2=1)
endif()
if(OPT_CDC_SYSVIEW)
    add_compile_definitions(OPT_CDC_SYSVIEW=1)
    target_sources(${PROJECT} PRIVATE
        src/cdc/cdc_sysview.c
    )
endif()

#--------------------------------------------------------------------------------------------------
#
# this is for SIGROK operation (taken from https://github.com/pico-coder/sigrok-pico)
#
if(OPT_SIGROK)
    add_compile_definitions(OPT_SIGROK=1)
    
    target_sources(${PROJECT} PRIVATE
        src/pico-sigrok/cdc_sigrok.c
        src/pico-sigrok/sigrok.c
        src/pico-sigrok/sigrok_int.c
    )

    target_link_libraries(${PROJECT} PRIVATE
        hardware_adc
        hardware_dma
    )

    pico_generate_pio_header(${PROJECT} ${CMAKE_CURRENT_LIST_DIR}/src/pico-sigrok/sigrok.pio)
endif()

#--------------------------------------------------------------------------------------------------
#
# this is for SystemView operation (requires RTT)
#
if(NOT OPT_NET STREQUAL "")
    if(OPT_NET STREQUAL "ECM")
        add_compile_definitions(OPT_NET_PROTO_ECM=1)
    elseif(OPT_NET STREQUAL "NCM")
        add_compile_definitions(OPT_NET_PROTO_NCM=1)
    elseif(OPT_NET STREQUAL "RNDIS")
        add_compile_definitions(OPT_NET_PROTO_RNDIS=1)
    else()
        message(FATAL_ERROR "OPT_NET must be either empty or one of ECM/NCM/RNDIS but is '${OPT_NET}'")
    endif()
    
    add_compile_definitions(OPT_NET=1)
    add_compile_definitions(OPT_NET_192_168=${OPT_NET_192_168})
    
    target_sources(${PROJECT} PRIVATE
        src/net/net_glue.c
        
        ${PICO_TINYUSB_PATH}/lib/networking/dhserver.c
    )

    if(OPT_NET STREQUAL "NCM")
        # NCM: use own copy of ncm_device, original one is removed from target_sources() with a trick
        message("--------- " ${PICO_TINYUSB_PATH})
        target_sources(${PROJECT} PRIVATE
            src/net/tinyusb/ncm_device_simple.c
        )
        set_source_files_properties(
            ${PICO_TINYUSB_PATH}/src/class/net/ncm_device.c
            PROPERTIES HEADER_FILE_ONLY ON
        )
    else()
        # !NCM
        target_sources(${PROJECT} PRIVATE
            ${PICO_TINYUSB_PATH}/lib/networking/rndis_reports.c
        )
    endif()
    
    target_include_directories(${PROJECT} PRIVATE
        ${PICO_TINYUSB_PATH}/lib/networking
    )
    
    target_link_libraries(${PROJECT} PRIVATE
        pico_lwip_freertos
    )

    if(OPT_NET_SYSVIEW_SERVER)
        add_compile_definitions(OPT_NET_SYSVIEW_SERVER=1)
        target_sources(${PROJECT} PRIVATE
            src/net/net_sysview.c
        )
    endif()
    
    if(OPT_NET_ECHO_SERVER)
        add_compile_definitions(OPT_NET_ECHO_SERVER=1)
        target_sources(${PROJECT} PRIVATE
            src/net/net_echo.c
        )
    endif()
    
    if(OPT_NET_IPERF_SERVER)
        add_compile_definitions(OPT_NET_IPERF_SERVER=1)
        target_sources(${PROJECT} PRIVATE
            ${PICO_LWIP_PATH}/src/apps/lwiperf/lwiperf.c
        )
    endif()
endif()

#--------------------------------------------------------------------------------------------------
#
# this is for MSC operation
#
if(OPT_MSC)
    add_compile_definitions(OPT_MSC=1)
    
    target_sources(${PROJECT} PRIVATE
        src/msc/msc_drive.c
        src/msc/msc_utils.c
    )
    
    if(OPT_MSC_RAM_UF2)
        add_compile_definitions(OPT_MSC_RAM_UF2=1)
    endif()
endif()

#--------------------------------------------------------------------------------------------------

target_compile_options(${PROJECT} PRIVATE -Wall)

if(DEFINED ENV{PICOPROBE_LED})
    message("PICOPROBE_LED is defined as " $ENV{PICOPROBE_LED})
    target_compile_definitions(${PROJECT} PRIVATE PICOPROBE_LED=$ENV{PICOPROBE_LED})
endif()

set(DBG_PIN_COUNT=4)

pico_generate_pio_header(${PROJECT} ${CMAKE_CURRENT_LIST_DIR}/src/probe.pio)

target_include_directories(${PROJECT} PRIVATE src)

target_compile_definitions (${PROJECT} PRIVATE
	PICO_RP2040_USB_DEVICE_ENUMERATION_FIX=1
	PICO_RP2040_USB_DEVICE_UFRAME_FIX=1
)

target_link_libraries(${PROJECT} PRIVATE
        pico_multicore
        pico_stdlib
        pico_unique_id
        tinyusb_device
        tinyusb_board
        hardware_pio
        FreeRTOS-Kernel
        FreeRTOS-Kernel-Heap4
)

if(PICO_BOARD STREQUAL "pico_w")
    target_link_libraries(${PROJECT} PRIVATE
        #pico_cyw43_arch_lwip_sys_freertos
        #pico_lwip_iperf
        pico_cyw43_arch_none
    )
endif()


#
# code resides in flash (in former versions it was in RAM)
# if you want to force single functions into RAM, use "__no_inline_not_in_flash_func()" from platform.h
# 
pico_set_binary_type(${PROJECT} default)

pico_add_extra_outputs(${PROJECT})
