cmake_minimum_required(VERSION 3.21)
project(jbox VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2 Network)
find_package(KF6WindowSystem REQUIRED)

qt_standard_project_setup(REQUIRES 6.5)

qt_add_executable(jbox
    src/main.cpp
    src/jsonstore.cpp
    src/historymodel.cpp
    src/aliasesmodel.cpp
    src/terminal.cpp
    src/launcherbackend.cpp
    src/ipcserver.cpp
    src/windowcontroller.cpp
)

qt_add_qml_module(jbox
    URI dev.jbox.app
    VERSION 1.0
    QML_FILES qml/Main.qml
)

target_link_libraries(jbox PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Qml
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::Network
    KF6::WindowSystem
)

install(TARGETS jbox RUNTIME DESTINATION bin)
