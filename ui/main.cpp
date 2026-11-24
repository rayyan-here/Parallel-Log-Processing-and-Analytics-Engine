#include "dashboard.hpp"
#include <QApplication>
#include <QCoreApplication>

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QApplication::setApplicationName("Parallel Log Engine");
    const auto args = QCoreApplication::arguments();
    const auto root = args.size() > 1 ? args.at(1) : QCoreApplication::applicationDirPath() + "/..";
    Dashboard window(root, args.value(2));
    window.show();
    return app.exec();
}
