# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# Copyright (C) 2026 Ezequiel Alves. All rights reserved.

# CPack Packaging Configuration for Condor

set(CPACK_PACKAGE_NAME "condor")
set(CPACK_PACKAGE_VENDOR "Ezequiel Alves")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Modern C++20 Parallel Process and Grid DAG Orchestrator")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/zkalves/condor")
set(CPACK_PACKAGE_CONTACT "Ezequiel Alves <alvesel@gmail.com>")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")

set(CPACK_PACKAGE_VERSION "1.0.0")
set(CPACK_PACKAGE_VERSION_MAJOR "1")
set(CPACK_PACKAGE_VERSION_MINOR "0")
set(CPACK_PACKAGE_VERSION_PATCH "0")

set(CPACK_STRIP_FILES TRUE)
set(CPACK_PACKAGING_INSTALL_PREFIX "/usr")
set(CPACK_GENERATOR "DEB;RPM;TGZ")
set(CPACK_PACKAGE_DIRECTORY "${CMAKE_BINARY_DIR}/packages")

# Debian (.deb) Configuration
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "Ezequiel Alves <alvesel@gmail.com>")
set(CPACK_DEBIAN_PACKAGE_SECTION "devel")
set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)

# RPM (.rpm) Configuration
set(CPACK_RPM_PACKAGE_LICENSE "GPL-3.0")
set(CPACK_RPM_PACKAGE_GROUP "Development/Tools")
set(CPACK_RPM_PACKAGE_URL "https://github.com/zkalves/condor")
set(CPACK_RPM_PACKAGE_AUTOREQPROV ON)
set(CPACK_RPM_FILE_NAME RPM-DEFAULT)

# Archive & Tarball naming
set(CPACK_ARCHIVE_FILE_NAME "condor-1.0.0-Linux")

# Ignore patterns for source packages
set(CPACK_SOURCE_GENERATOR "TGZ;ZIP")
set(CPACK_SOURCE_IGNORE_FILES
    "/.git/"
    "/build/"
    "/_site/"
    "/condor_runs/"
    "/test_.*/"
    ".*~$"
)

include(CPack)
