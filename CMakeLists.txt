# Copyright (c) 2021, Sergei Ilinykh <rion4ik@gmail.com>
# Thanks to Richard Hodges for sanitizers
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
#
# 1. Redistributions of source code must retain the above copyright notice, this
#    list of conditions and the following disclaimer.
# 2. Redistributions in binary form must reproduce the above copyright notice,
#    this list of conditions and the following disclaimer in the documentation
#    and/or other materials provided with the distribution.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
# ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
# WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
# DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
# ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
# (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
# LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
# ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
# SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

cmake_minimum_required(VERSION 3.17)

project(restio VERSION 1.0.0 DESCRIPTION "Simple to use C++/Boost REST server")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_MODULE_PATH "${CMAKE_MODULE_PATH}" "${CMAKE_CURRENT_SOURCE_DIR}/cmake/modules")

option(ENABLE_SANITIZERS OFF)
if (ENABLE_SANITIZERS)
    set(CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/externals/sanitizers-cmake/cmake" ${CMAKE_MODULE_PATH})
    option(SANITIZE_ADDRESS OFF)
    option(SANITIZE_MEMORY OFF)
    option(SANITIZE_THREAD OFF)
    option(SANITIZE_UNDEFINED OFF)
    find_package(Sanitizers)
else()
    function(add_sanitizers)
    endfunction()
endif()

option(BUILD_TOOLS "Build example tools" ON)
option(BUILD_TESTS "Build unit tests" ON)
option(RESTIO_BUILD_STATIC "Build static restio library" ON)
option(RESTIO_BUILD_SHARED "Build shared restio library" OFF)
option(RESTIO_INSTALL "Setup library install rules (otherwise just build)" ON)
option(QT_CREATOR_COROUTINE_COMPAT "Enable some defines to sarisfy Qt Creator abalyzer" OFF)

if(RESTIO_BUILD_STATIC)
    set(RESTIO_LIB_SUFFIX "_static")
elseif(RESTIO_BUILD_SHARED)
    set(RESTIO_LIB_SUFFIX "")
else()
    message(FATAL_ERROR "All libraries types were disabled. Nothing to build.")
endif()

include(GNUInstallDirs)
add_subdirectory(src)
if (BUILD_TOOLS)
    add_subdirectory(tools)
endif()
if (BUILD_TESTS)
    add_subdirectory(test)
endif()

install(FILES LICENSE TYPE DOC)

# packaging

if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
  set(CPACK_PACKAGE_CONTACT "rion4ik@@gmail.com")
  set(CPACK_PACKAGE_DESCRIPTION "${CMAKE_PROJECT_DESCRIPTION}")
  set(CPACK_PACKAGE_RELOCATABLE OFF)
  set(CPACK_PACKAGE_VENDOR "Sergei Ilinykh")

  set(CPACK_RESOURCE_FILE_LICENSE ${CMAKE_SOURCE_DIR}/LICENSE)

  set(CPACK_DEBIAN_PACKAGE_GENERATE_SHLIBS ON)
  set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
  set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)

  set(CPACK_RPM_PACKAGE_AUTOREQPROV ON)
  set(CPACK_RPM_FILE_NAME RPM-DEFAULT)

  include(CPack)
endif()
