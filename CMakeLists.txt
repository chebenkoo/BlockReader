cmake_minimum_required(VERSION 3.16)

project(MiningSchedule VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD_REQUIRED ON)

# --- CGAL Integration ---
set(Boost_DEBUG ON)

# 1. Verify that the Boost path is actually correct
if(NOT EXISTS "C:/boost_1_90_0/boost/version.hpp")
    message(FATAL_ERROR "CRITICAL: 'C:/boost_1_90_0' is invalid.")
endif()

# 2. Config Boost
# Clear any cached values that point to the old version
unset(Boost_INCLUDE_DIR CACHE)
unset(Boost_LIBRARY_DIRS CACHE)
unset(Boost_DIR CACHE)

set(BOOST_ROOT "C:/boost_1_90_0")
set(Boost_NO_SYSTEM_PATHS ON) # Ignore 1.66
set(Boost_NO_BOOST_CMAKE ON)  # Don't look for BoostConfig.cmake

# 3. CGAL
set(CGAL_DIR "${CMAKE_CURRENT_SOURCE_DIR}/CGAL-6.1.1")
find_package(CGAL REQUIRED)

# --- Qt Quick 3D ---
find_package(Qt6 REQUIRED COMPONENTS Quick Quick3D)

qt_standard_project_setup(REQUIRES 6.8)

qt_add_executable(appMiningSchedule
    main.cpp
)

qt_add_qml_module(appMiningSchedule
    URI MiningSchedule
    QML_FILES
        Main.qml
)

# Qt for iOS sets MACOSX_BUNDLE_GUI_IDENTIFIER automatically since Qt 6.1.
# If you are developing for iOS or macOS you should consider setting an
# explicit, fixed bundle identifier manually though.
set_target_properties(appMiningSchedule PROPERTIES
#    MACOSX_BUNDLE_GUI_IDENTIFIER com.example.appMiningSchedule
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
)

target_link_libraries(appMiningSchedule
    PRIVATE 
    Qt6::Quick
    Qt6::Quick3D
    CGAL::CGAL
)

include(GNUInstallDirs)
install(TARGETS appMiningSchedule
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
