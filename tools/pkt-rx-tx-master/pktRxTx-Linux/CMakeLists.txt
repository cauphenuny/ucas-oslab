cmake_minimum_required (VERSION 3.5)
project (pktRxTx)

add_compile_options(-g -O3)

find_package(PkgConfig REQUIRED)
# find libpcap
pkg_check_modules(PCAP REQUIRED libpcap)
# try to find libnet (libnet or libnet-1.1)
pkg_check_modules(LIBNET QUIET libnet libnet-1.1)

# include headers from project and found packages
include_directories(${PROJECT_SOURCE_DIR}/Include ${PCAP_INCLUDE_DIRS} ${LIBNET_INCLUDE_DIRS})

aux_source_directory(./Source SRCS)

execute_process(COMMAND uname -s OUTPUT_VARIABLE KERNEL OUTPUT_STRIP_TRAILING_WHITESPACE)
execute_process(COMMAND uname -m OUTPUT_VARIABLE ARCH OUTPUT_STRIP_TRAILING_WHITESPACE)
set(BIN_NAME "pktRxTx-${KERNEL}-${ARCH}")
add_executable(pktRxTx ${SRCS})
# place runtime binary in repository root
set_target_properties(pktRxTx PROPERTIES OUTPUT_NAME ${BIN_NAME} RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/../")

# prepare libraries to link
set(ADDITIONAL_LIBS ${PCAP_LIBRARIES})
if(LIBNET_FOUND)
    list(APPEND ADDITIONAL_LIBS ${LIBNET_LIBRARIES})
else()
    if(APPLE)
        # try common Homebrew /usr paths on macOS
        find_library(LIBNET_FALLBACK NAMES net libnet libnet-1.1 PATHS /usr/local/lib /opt/homebrew/lib /usr/lib)
        if(LIBNET_FALLBACK)
            list(APPEND ADDITIONAL_LIBS ${LIBNET_FALLBACK})
            message(STATUS "Found libnet via find_library: ${LIBNET_FALLBACK}")
        else()
            message(WARNING "libnet not found via pkg-config or find_library; build will continue without libnet. Install libnet (brew install libnet) to enable it.")
        endif()
    else()
        message(WARNING "libnet not found via pkg-config; build will continue without libnet. Install libnet-dev or set PKG_CONFIG_PATH.")
    endif()
endif()

list(APPEND ADDITIONAL_LIBS -lpthread)

target_link_libraries(pktRxTx ${ADDITIONAL_LIBS})
