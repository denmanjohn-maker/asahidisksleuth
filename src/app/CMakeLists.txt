find_package(ECM REQUIRED NO_MODULE)
set(CMAKE_MODULE_PATH ${ECM_MODULE_PATH})

find_package(Qt6 REQUIRED COMPONENTS Quick Qml Widgets)
find_package(KF6 REQUIRED COMPONENTS Kirigami CoreAddons I18n Crash)

qt_add_executable(asahidisksleuth-app
    main.cpp
    ScanController.cpp
    ScanController.h
    SunburstModel.cpp
    SunburstModel.h
    SunburstLayout.cpp
    SunburstLayout.h
)

qt_add_qml_module(asahidisksleuth-app
    URI asahidisksleuth
    VERSION 1.0
    QML_FILES
        qml/Main.qml
        qml/WelcomePage.qml
        qml/ResultsPage.qml
        qml/Sunburst.qml
        qml/Inspector.qml
)

target_link_libraries(asahidisksleuth-app PRIVATE
    asahidisksleuth-core
    Qt6::Quick Qt6::Qml Qt6::Widgets
    KF6::Kirigami KF6::CoreAddons KF6::I18n KF6::Crash
)

install(TARGETS asahidisksleuth-app RUNTIME DESTINATION bin)
install(FILES ${CMAKE_SOURCE_DIR}/packaging/asahidisksleuth.desktop
        DESTINATION share/applications)
install(FILES ${CMAKE_SOURCE_DIR}/packaging/org.asahidisksleuth.app.metainfo.xml
        DESTINATION share/metainfo)
