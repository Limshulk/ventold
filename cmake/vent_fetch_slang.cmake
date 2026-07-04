# vent build system.
# ——————————————————————
# fetches the slang compiler binaries if not found locally.

include_guard(GLOBAL)

set(VENT_SLANG_VERSION "2026.12.2")

find_program(VENT_SLANGC_EXECUTABLE slangc)

if(VENT_SLANGC_EXECUTABLE)
    vent_log("found slang compiler: ${VENT_SLANGC_EXECUTABLE}")
else()
    vent_log("slang compiler not found, fetching from github...")

    set(SLANG_BASE_URL "https://github.com/shader-slang/slang/releases/download/v${VENT_SLANG_VERSION}")
    
    if(VENT_PLATFORM_WINDOWS)
        set(SLANG_FILENAME "slang-${VENT_SLANG_VERSION}-windows-x86_64.zip")
    elseif(VENT_PLATFORM_LINUX)
        set(SLANG_FILENAME "slang-${VENT_SLANG_VERSION}-linux-x86_64.tar.gz")
    elseif(VENT_PLATFORM_MACOS)
        set(SLANG_FILENAME "slang-${VENT_SLANG_VERSION}-macos-aarch64.tar.gz")
    endif()

    set(SLANG_DOWNLOAD_URL "${SLANG_BASE_URL}/${SLANG_FILENAME}")
    set(SLANG_DEST_DIR "${CMAKE_SOURCE_DIR}/build/third_party/slang")
    set(SLANG_ARCHIVE "${CMAKE_BINARY_DIR}/third_party_downloads/${SLANG_FILENAME}")

    if(NOT EXISTS "${SLANG_DEST_DIR}/bin/slangc.exe" AND NOT EXISTS "${SLANG_DEST_DIR}/bin/slangc")
        vent_log("downloading slang ${VENT_SLANG_VERSION} from ${SLANG_DOWNLOAD_URL}...")
        file(DOWNLOAD "${SLANG_DOWNLOAD_URL}" "${SLANG_ARCHIVE}" SHOW_PROGRESS STATUS DOWNLOAD_STATUS)
        
        list(GET DOWNLOAD_STATUS 0 STATUS_CODE)
        if(STATUS_CODE EQUAL 0)
            vent_log("extracting slang...")
            file(MAKE_DIRECTORY "${SLANG_DEST_DIR}")
            file(ARCHIVE_EXTRACT INPUT "${SLANG_ARCHIVE}" DESTINATION "${SLANG_DEST_DIR}")
        else()
            list(GET DOWNLOAD_STATUS 1 ERROR_MSG)
            message(FATAL_ERROR "[vent] failed to download slang: ${ERROR_MSG}")
        endif()
    endif()

    if(VENT_PLATFORM_WINDOWS)
        set(VENT_SLANGC_EXECUTABLE "${SLANG_DEST_DIR}/bin/slangc.exe")
    else()
        set(VENT_SLANGC_EXECUTABLE "${SLANG_DEST_DIR}/bin/slangc")
    endif()
    
    if(NOT EXISTS "${VENT_SLANGC_EXECUTABLE}")
        message(FATAL_ERROR "[vent] slangc executable not found after extraction: ${VENT_SLANGC_EXECUTABLE}")
    endif()
    
    vent_log("using downloaded slang compiler: ${VENT_SLANGC_EXECUTABLE}")
endif()
