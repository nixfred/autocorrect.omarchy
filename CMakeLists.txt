cmake_minimum_required(VERSION 3.16)
project(autocorrect-omarchy VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
include(GNUInstallDirs)

find_package(Fcitx5Core REQUIRED)

# Pure logic, no fcitx5: shared by the module and the tests.
add_library(corrector STATIC src/corrector.cpp)
set_target_properties(corrector PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(corrector PUBLIC src)

add_library(autocorrect MODULE src/autocorrect.cpp)
set_target_properties(autocorrect PROPERTIES PREFIX "lib")
target_link_libraries(autocorrect PRIVATE corrector Fcitx5::Core)

configure_file(autocorrect.conf.in autocorrect.conf @ONLY)

install(TARGETS autocorrect DESTINATION "${CMAKE_INSTALL_LIBDIR}/fcitx5")
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/autocorrect.conf"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/fcitx5/addon")

enable_testing()
add_executable(corrector_test tests/corrector_test.cpp)
target_link_libraries(corrector_test PRIVATE corrector)
add_test(NAME corrector_test COMMAND corrector_test)

# Headless: real fcitx5 Instance + testim + our .so, typing into a fake app.
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/testdata/addon")
configure_file(autocorrect.conf.in testdata/addon/autocorrect.conf @ONLY)
add_executable(module_test tests/module_test.cpp)
target_link_libraries(module_test PRIVATE Fcitx5::Core)
add_dependencies(module_test autocorrect)
add_test(NAME module_test COMMAND module_test)
