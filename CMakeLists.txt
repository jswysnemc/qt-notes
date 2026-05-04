cmake_minimum_required(VERSION 3.21)

project(qt-notes VERSION 0.1.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

find_package(PkgConfig REQUIRED)
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Sql LinguistTools)
pkg_check_modules(Libsodium REQUIRED IMPORTED_TARGET libsodium)
pkg_check_modules(Libsecret REQUIRED IMPORTED_TARGET libsecret-1)

set(TS_FILES
    translations/qt-notes_zh_CN.ts
    translations/qt-notes_en.ts
)

set(QM_FILES
    ${CMAKE_CURRENT_BINARY_DIR}/qt-notes_zh_CN.qm
    ${CMAKE_CURRENT_BINARY_DIR}/qt-notes_en.qm
)

qt_add_executable(qt-notes
    src/main.cpp
    src/app/applicationcontroller.cpp
    src/app/applicationcontroller.h
    src/common/storagepaths.h
    src/data/notedata.h
    src/data/notesrepository.cpp
    src/data/notesrepository.h
    src/model/notelistmodel.cpp
    src/model/notelistmodel.h
    src/security/notecrypto.cpp
    src/security/notecrypto.h
    src/security/unlockstatestore.cpp
    src/security/unlockstatestore.h
    src/theme/themecatalog.cpp
    src/theme/themecatalog.h
    src/ui/iconfactory.cpp
    src/ui/iconfactory.h
    src/ui/noteeditor.cpp
    src/ui/noteeditor.h
    src/ui/noteencryptiondialog.cpp
    src/ui/noteencryptiondialog.h
    src/ui/notelistpopup.cpp
    src/ui/notelistpopup.h
    src/ui/settingsdialog.cpp
    src/ui/settingsdialog.h
    src/ui/titlebar.cpp
    src/ui/titlebar.h
    src/ui/notewindow.cpp
    src/ui/notewindow.h
)

qt_add_translations(qt-notes
    TS_FILES ${TS_FILES}
    RESOURCE_PREFIX "/i18n"
)

target_include_directories(qt-notes PRIVATE src)

target_link_libraries(qt-notes PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Sql
    PkgConfig::Libsodium
    PkgConfig::Libsecret
)

install(TARGETS qt-notes RUNTIME DESTINATION bin)
install(FILES ${QM_FILES} DESTINATION share/qt-notes/translations)
