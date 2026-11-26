cmake_minimum_required(VERSION 3.18)

project(fcitx5-android-plugin-mozc VERSION ${VERSION_NAME})

# For reproducible build
add_link_options("LINKER:--hash-style=gnu,--build-id=none")

# prefab dependency
find_package(fcitx5 REQUIRED CONFIG)
get_target_property(FCITX5_CMAKE_MODULES fcitx5::cmake INTERFACE_INCLUDE_DIRECTORIES)
set(CMAKE_MODULE_PATH ${FCITX5_CMAKE_MODULES} ${CMAKE_MODULE_PATH})

find_package(ECM MODULE)
find_package(Fcitx5Core MODULE)

include("${FCITX_INSTALL_CMAKECONFIG_DIR}/Fcitx5Utils/Fcitx5CompilerSettings.cmake")

# Prebuilt mozc engine library (bazel-built from the mozc sources).
add_library(mozc_engine SHARED IMPORTED)
set_target_properties(mozc_engine PROPERTIES
    IMPORTED_LOCATION "${CMAKE_CURRENT_SOURCE_DIR}/../jniLibs/${ANDROID_ABI}/libmozc_engine.so")

add_fcitx5_addon(mozc mozc.cpp)
target_link_libraries(mozc Fcitx5::Core Fcitx5::Config)
target_include_directories(mozc PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
install(TARGETS mozc DESTINATION "${CMAKE_INSTALL_LIBDIR}/fcitx5")

configure_file(mozc-addon.conf.in mozc-addon.conf)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/mozc-addon.conf"
        RENAME mozc.conf
        DESTINATION "${FCITX_INSTALL_PKGDATADIR}/addon"
        COMPONENT config)
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/mozc.conf"
        DESTINATION "${FCITX_INSTALL_PKGDATADIR}/inputmethod"
        COMPONENT config)
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/prebuilt/mozc.data"
        DESTINATION "${FCITX_INSTALL_DATADIR}/mozc"
        COMPONENT prebuilt-assets)
