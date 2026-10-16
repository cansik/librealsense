# License: Apache 2.0. See LICENSE file in root directory.
if(NOT APPLE)
    message(FATAL_ERROR "MACOS_USB_DRIVERKIT requires macOS")
endif()
target_sources(${LRS_TARGET} PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/usb-driverkit.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/transport/src/Client.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/transport/src/Descriptors.cpp")
target_include_directories(${LRS_TARGET} PRIVATE "${CMAKE_CURRENT_LIST_DIR}/transport/include")
target_compile_definitions(${LRS_TARGET} PRIVATE RS2_MACOS_DRIVERKIT)
target_link_libraries(${LRS_TARGET} PRIVATE "-framework IOKit" "-framework CoreFoundation")
