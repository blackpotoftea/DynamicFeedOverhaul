vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO MinLL/CommonLibVR
    REF v4.39.5
    SHA512 6eb01e5ebbf2216d1fa33b63afa9363e8dc07a2afcb510157b47baa9d66a3d44d5e5571cb42bea29899b8dc329c69e628dcf81f371dfb40c740763e008ef3809
    HEAD_REF ng
)

# extern/openvr is a git submodule, which GitHub archives omit; pinned to the commit v4.39.5 references.
vcpkg_from_github(
    OUT_SOURCE_PATH OPENVR_SOURCE_PATH
    REPO ValveSoftware/openvr
    REF 60eb187801956ad277f1cae6680e3a410ee0873b
    SHA512 bb85b4705e7095ac65df9969112b2df8930cee7917cc5f14231c5a0ffeed7a73ffa60727fd32f8786a403656f95a3ec0f80bf3ceabc5b8ede964aefb920bc718
    HEAD_REF master
)

file(GLOB OPENVR_FILES "${OPENVR_SOURCE_PATH}/*")
file(COPY ${OPENVR_FILES} DESTINATION "${SOURCE_PATH}/extern/openvr")

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS -DBUILD_TESTS=OFF -DSKSE_SUPPORT_XBYAK=ON
)

vcpkg_cmake_install()
vcpkg_cmake_config_fixup(PACKAGE_NAME CommonLibSSE CONFIG_PATH lib/cmake)
vcpkg_copy_pdbs()

file(INSTALL "${OPENVR_SOURCE_PATH}/headers/openvr.h" DESTINATION "${CURRENT_PACKAGES_DIR}/include")
file(GLOB CMAKE_CONFIGS "${CURRENT_PACKAGES_DIR}/share/CommonLibSSE/CommonLibSSE/*.cmake")
file(INSTALL ${CMAKE_CONFIGS} DESTINATION "${CURRENT_PACKAGES_DIR}/share/CommonLibSSE")
file(INSTALL "${SOURCE_PATH}/cmake/CommonLibSSE.cmake" DESTINATION "${CURRENT_PACKAGES_DIR}/share/CommonLibSSE")

file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include")
file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/share/CommonLibSSE/CommonLibSSE")

file(INSTALL "${SOURCE_PATH}/LICENSE" DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}" RENAME copyright)
