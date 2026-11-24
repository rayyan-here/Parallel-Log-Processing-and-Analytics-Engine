#include "dashboard.hpp"
#include <QApplication>
#include <QComboBox>
#include <QElapsedTimer>
#include <QFile>
#include <QFileDialog>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <functional>
#include <iostream>
#include <stdexcept>

namespace {
void check(bool ok, const char* message) {
    if (!ok) throw std::runtime_error(message);
}

void waitFor(const std::function<bool()>& condition) {
    QElapsedTimer elapsed;
    elapsed.start();
    while (!condition() && elapsed.elapsed() < 30000) {
        QApplication::processEvents(QEventLoop::AllEvents, 30);
        QThread::msleep(1);
    }
    check(condition(), "Timed out waiting for the Qt event loop.");
}

template<class T>
T* control(Dashboard& view, const QString& name) {
    T* item = view.findChild<T*>(name);
    if (!item) throw std::runtime_error(("Missing control " + name).toStdString());
    return item;
}

// The button opens its real dialog. Supply a deterministic choice inside its nested event loop.
void answerFileDialog(const QString& path) {
    QTimer::singleShot(0, [path] {
        auto* dialog = qobject_cast<QFileDialog*>(QApplication::activeModalWidget());
        if (!dialog) qFatal("No file dialog appeared");
        dialog->selectFile(path);
        QMetaObject::invokeMethod(dialog, "accept", Qt::QueuedConnection);
    });
}
}

int main(int argc, char** argv) {
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    QApplication app(argc, argv);
    try {
        QTemporaryDir tmp;
        check(tmp.isValid(), "Temporary directory");
        Dashboard window(QDir::currentPath());
        window.show();
        QApplication::processEvents();
        const auto input = tmp.filePath("small log.txt");
        check(QFile::copy("data/sample.txt", input), "Copy sample");
        auto* files = control<QComboBox>(window, "files");
        auto* browse = control<QPushButton>(window, "browse");
        auto* run = control<QPushButton>(window, "run");
        auto* save = control<QPushButton>(window, "exportCsv");
        auto* status = control<QLabel>(window, "status");
        auto* w = control<QSpinBox>(window, "workers");
        auto* r = control<QSpinBox>(window, "readers");
        auto* segments = control<QTableWidget>(window, "segments");
        auto* ids = control<QTableWidget>(window, "identities");
        auto* progress = control<QProgressBar>(window, "progress");
        auto value = [&](const QString& key) { return control<QLabel>(window, key)->text(); };
        auto analyze = [&] {
            run->click();
            check(!run->isEnabled() && !save->isEnabled(), "Busy state");
            waitFor([&] { return run->isEnabled(); });
            check(save->isEnabled() && status->text().startsWith("Verified"), "Verified run");
        };
        check(!save->isEnabled(), "Stale export on startup");
        answerFileDialog(input);
        browse->click();
        check(files->currentData().toString() == input, "Browse action");
        analyze();
        check(value("processed") == "24" && value("failed") == "9" && value("flagged") == "2"
              && value("unattributed") == "1" && value("patternCount") == "2", "Sample counters");
        check(value("INFOCount") == "10" && value("WARNINGCount") == "5"
              && value("ERRORCount") == "7" && value("OTHERCount") == "2", "Sample severities");
        check(control<QProgressBar>(window,"INFOBar")->value() == 417 &&
              control<QProgressBar>(window,"WARNINGBar")->value() == 208 &&
              control<QProgressBar>(window,"ERRORBar")->value() == 292, "Bar denominators");
        check(progress->value() == 1000 && segments->rowCount() == 4 && ids->rowCount() == 2,
              "Segment or identity rows");
        check(segments->item(0, 2)->text() == "[0, 241)" && segments->item(3, 1)->text() == "19–24"
              && segments->item(3, 3)->text() == "3" && ids->item(1, 0)->text() == "user=ali"
              && ids->item(1, 1)->text() == "3", "Worker and identity details");
        check(value("indexTime").endsWith(" ms") && value("singleTime").endsWith(" ms") && value("parallelTime").endsWith(" ms")
              && value("speedup").endsWith("×") && value("peak").endsWith("/ 2"), "Timing panel");
        // An optional output path captures the actual verified dashboard for the project report.
        if (argc == 2) {
            files->setCurrentIndex(0);
            analyze();
            QApplication::processEvents();
            check(window.grab().save(QString::fromLocal8Bit(argv[1])), "Screenshot");
        }
        const auto csv = tmp.filePath("saved report.csv");
        answerFileDialog(tmp.filePath("saved report"));
        save->click();
        QFile report(csv);
        check(report.open(QIODevice::ReadOnly), "Export button did not save");
        const auto rows = report.readAll();
        check(rows.contains("\"flagged_identity\",\"user=ali\",\"3\"")
              && rows.contains("\"validation\",\"counts_and_identities_match\",\"yes\""), "Export values");

        files->setCurrentIndex(1);
        check(!save->isEnabled() && ids->rowCount() == 0, "Stale results on input change");
        bool singleUpdate = false, multiUpdate = false;
        QObject::connect(progress, &QProgressBar::valueChanged, &window, [&](int n) {
            if (n <= 0) return;
            if (status->text().startsWith("Single-worker")) singleUpdate = true;
            if (status->text().startsWith("Parallel analysis")) multiUpdate = true;
        });
        analyze();
        check(value("processed") == "500,000" && value("failed") == "100,000"
              && singleUpdate && multiUpdate, "Live demonstration or its progress signals");

        w->setValue(2);
        r->setValue(1);
        check(r->maximum() == 2 && !save->isEnabled(), "Invalidation on settings change");
        analyze();
        check(value("peak") == "1 / 1" && segments->rowCount() == 2, "Single reader permit");

        files->setCurrentIndex(2);
        analyze();
        check(value("processed") == "9" && value("patternCount") == "2" && value("flagged") == "1"
              && value("unattributed") == "3 (1 alert)" && ids->rowCount() == 1
              && ids->item(0,0)->text() == "user=lab user", "Quoted user and unattributed alert");
        const auto spaced = tmp.filePath("tab.txt");
        QFile spacedFile(spaced);
        check(spacedFile.open(QIODevice::WriteOnly), "Quoted tab fixture");
        for (int n = 0; n < 3; ++n) spacedFile.write("ERROR FAILED_LOGIN user=\"ops\tdesk\"\n");
        spacedFile.close();
        answerFileDialog(spaced);
        browse->click();
        analyze();
        check(ids->rowCount() == 1 && ids->item(0,0)->text() == "user=ops\tdesk"
              && ids->item(0,1)->text() == "3", "Tab in quoted identity");

        const auto many = tmp.filePath("many.txt");
        QFile fixture(many);
        check(fixture.open(QIODevice::WriteOnly), "Many-identity fixture");
        for (int id = 0; id < 10; ++id)
            for (int n = 0; n < 3; ++n) fixture.write("ERROR FAILED_LOGIN user=u" + QByteArray::number(id) + "\n");
        fixture.close();
        answerFileDialog(many);
        browse->click();
        analyze();
        check(value("flagged") == "10" && ids->rowCount() == 8 && value("flagNote").contains("10"), "Preview cap");
        const auto manyCsv = tmp.filePath("all.csv");
        answerFileDialog(manyCsv);
        save->click();
        QFile full(manyCsv);
        check(full.open(QIODevice::ReadOnly) && full.readAll().count("\"flagged_identity\"") == 10, "Full CSV");
        check(QFile::remove(many), "Remove test fixture");
        run->click();
        check(!save->isEnabled() && status->text().contains("readable"), "Missing-file error");
        files->setCurrentIndex(0);
        analyze();
        // An empty file is a valid completed comparison.
        const auto empty = tmp.filePath("empty.txt");
        QFile emptyFile(empty);
        check(emptyFile.open(QIODevice::WriteOnly), "Empty fixture");
        emptyFile.close();
        answerFileDialog(empty);
        browse->click();
        analyze();
        check(value("processed") == "0" && value("patternCount") == "0" && progress->value() == 1000 && ids->rowCount() == 0
              && segments->item(0,1)->text() == "Empty", "Empty input");
        std::cout << "PASS Qt Widgets: browse, real runs, live bars, worker ranges, flagged identities, "
                     "export, reader cap, settings, empty file and error recovery\n";
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
