# macOS application bundle and distributable package.

set_target_properties (${PROJECT_NAME} PROPERTIES
    MACOSX_BUNDLE TRUE
    MACOSX_BUNDLE_BUNDLE_NAME ${PROJECT_NAME}
    MACOSX_BUNDLE_GUI_IDENTIFIER ${MACOS_GUI_IDENFIER}
    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/macOS/Info.plist.in"
)

install (TARGETS ${PROJECT_NAME}
    BUNDLE DESTINATION .
)

# Copy Homebrew-installed dylibs into the bundle and rewrite their install names
# so the app does not depend on the build machine's /opt/homebrew.
install (CODE [[
    include (BundleUtilities)
    fixup_bundle (
        "${CMAKE_INSTALL_PREFIX}/${PROJECT_NAME}.app/Contents/MacOS/${PROJECT_NAME}"
        ""
        "${CMAKE_INSTALL_PREFIX}/${PROJECT_NAME}.app/Contents/Frameworks"
    )
]])

set (CPACK_PACKAGE_NAME ${PROJECT_NAME})
set (CPACK_PACKAGE_VENDOR ${AUTHOR})
set (CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set (CPACK_GENERATOR "DragNDrop")
include (CPack)
