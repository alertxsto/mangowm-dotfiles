QT += core gui qml quick network testlib
CONFIG += testcase c++17
SOURCES += networkcontroller-test.cpp
INCLUDEPATH += $$OUT_PWD

# Make the generated controller meta-object visible to qmake's dependency scanner.
!system($$shell_quote($$[QT_INSTALL_LIBEXECS]/moc) $$shell_quote($$PWD/../main.cpp) -o $$shell_quote($$OUT_PWD/main.moc)) {
    error(Failed to generate the network controller meta-object)
}

controller_moc.target = main.moc
controller_moc.depends = $$PWD/../main.cpp
controller_moc.commands = $$[QT_INSTALL_LIBEXECS]/moc $$PWD/../main.cpp -o main.moc
QMAKE_EXTRA_TARGETS += controller_moc
PRE_TARGETDEPS += main.moc
