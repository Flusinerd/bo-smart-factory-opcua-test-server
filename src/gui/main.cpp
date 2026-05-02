#include <QApplication>
#include "MainWindow.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName("OPC UA TCP Bridge");
    MainWindow w;
    w.show();
    return app.exec();
}
