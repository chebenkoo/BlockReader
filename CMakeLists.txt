cmake_minimum_required(VERSION 3.16)

# Force CMake to use our fake Boost config to satisfy CGAL's picky requirements
list(APPEND CMAKE_PREFIX_PATH "${CMAKE_CURRENT_SOURCE_DIR}/third_party/boost-fake")
set(Boost_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/boost-fake")

if(POLICY CMP0167)
    cmake_policy(SET CMP0167 OLD)
endif()

project(MiningSchedule VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
    add_compile_definitions(NOMINMAX)          # prevent windows.h min/max macros globally
    add_compile_options(/utf-8)                # treat source files as UTF-8
    add_compile_options(/MP)                   # parallel compilation
    add_compile_options(/wd4267 /wd4244)       # suppress common narrowing conversion warnings
endif()

# --- CGAL Configuration ---
set(CGAL_HEADER_ONLY ON)
set(CGAL_DIR "${CMAKE_CURRENT_SOURCE_DIR}/CGAL-6.1.1")
find_package(Boost 1.74 REQUIRED CONFIG) 
find_package(CGAL REQUIRED)

# --- Qt Quick 3D ---
find_package(Qt6 REQUIRED COMPONENTS Quick Quick3D Core Quick3DHelpers)
qt_standard_project_setup(REQUIRES 6.8)

set(BLOCK_MODEL_SOURCES
    include/BlockModel/BlockModel.h
    include/BlockModel/Reader.h
    include/BlockModel/MicromineReader.h
    include/BlockModel/SubprocessReader.h
    include/BlockModel/SpatialIndex.h
    include/BlockModel/Instancing.h
    include/BlockModel/Optimizer.h
    include/BlockModel/FormulaEvaluator.h
    include/BlockModel/AnalyticsEngine.h
    src/BlockModel/Reader.cpp
    src/BlockModel/SpatialIndex.cpp
    src/BlockModel/Instancing.cpp
    src/BlockModel/MicromineReader.cpp
    src/BlockModel/SubprocessReader.cpp
    src/BlockModel/Optimizer.cpp
    src/BlockModel/FormulaEvaluator.cpp
    src/BlockModel/AnalyticsEngine.cpp
)

qt_add_executable(appMiningSchedule
    main.cpp
    ${BLOCK_MODEL_SOURCES}
)

target_include_directories(appMiningSchedule PRIVATE 
    include
    third_party
)

qt_add_qml_module(appMiningSchedule
    URI MiningSchedule
    QML_FILES
        Main.qml
        AttributeRow.qml
        FormulaRow.qml
        BlockPropertyPanel.qml
        AnalyticsPanel.qml
)

target_link_libraries(appMiningSchedule
    PRIVATE 
    Qt6::Quick
    Qt6::Quick3D
    Qt6::Quick3DHelpers
    CGAL::CGAL
)

# --- POST-BUILD Deployment of Proxy & DLLs ---
set(SDK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/bin/MicromineSDK")
set(PROXY_EXE "${CMAKE_CURRENT_SOURCE_DIR}/MicromineProxy.exe")

set(SDK_FILES
    MicromineWrapper.dll 
    DbText.dll
    AParser.dll 
    fieldname.dll 
    format.dll 
    mmsystem.dll 
    UnitSystem.dll
    MmBlockModel.dll 
    TriStore.dll 
    roaring.dll
    icudt70.dll 
    icuin70.dll 
    icuio70.dll 
    icutu70.dll 
    icuuc70.dll
    zlibwapi.dll
    zlibwapid.dll
    sqlite3.dll
    MSVCP140.dll
    VCRUNTIME140.dll
    VCRUNTIME140_1.dll
)

# 1. Copy Proxy EXE
if(EXISTS "${PROXY_EXE}")
    add_custom_command(TARGET appMiningSchedule POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${PROXY_EXE}" "$<TARGET_FILE_DIR:appMiningSchedule>/MicromineProxy.exe")
endif()

# 2. Copy SDK DLLs
foreach(FILE ${SDK_FILES})
    if(EXISTS "${SDK_DIR}/${FILE}")
        add_custom_command(TARGET appMiningSchedule POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${SDK_DIR}/${FILE}" "$<TARGET_FILE_DIR:appMiningSchedule>/${FILE}")
    else()
        message(WARNING "Deployment: ${FILE} not found in bin/MicromineSDK, skipping copy.")
    endif()
endforeach()

include(GNUInstallDirs)
install(TARGETS appMiningSchedule
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
