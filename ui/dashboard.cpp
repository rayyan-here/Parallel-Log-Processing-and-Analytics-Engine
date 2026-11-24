#include "dashboard.hpp"
#include "engine.hpp"
#include <QComboBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QLabel>
#include <QLocale>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QTableWidget>
#include <QVBoxLayout>

namespace {
QString number(quint64 value) { return QLocale(QLocale::English).toString(value); }

int perThousand(quint64 part, quint64 whole) {
    return whole ? qRound(1000.0 * static_cast<double>(part) / static_cast<double>(whole)) : 0;
}

QLabel* label(const QString& text, const QString& name = {}) {
    auto* value = new QLabel(text);
    value->setTextFormat(Qt::PlainText);
    value->setObjectName(name);
    return value;
}

QTableWidget* table(const QStringList& headings, const QString& name) {
    auto* view = new QTableWidget(0, headings.size());
    view->setObjectName(name);
    view->setHorizontalHeaderLabels(headings);
    view->verticalHeader()->hide();
    view->verticalHeader()->setDefaultSectionSize(23);
    view->horizontalHeader()->setHighlightSections(false);
    view->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    view->horizontalHeader()->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    view->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Ignored);
    view->setShowGrid(false);
    view->setAlternatingRowColors(true);
    view->setSelectionBehavior(QAbstractItemView::SelectRows);
    view->setEditTriggers(QAbstractItemView::NoEditTriggers);
    view->setFocusPolicy(Qt::NoFocus);
    return view;
}

void cell(QTableWidget* view, int row, int column, const QString& text) {
    auto* value = new QTableWidgetItem(text);
    value->setToolTip(text);
    view->setItem(row, column, value);
}
}

Dashboard::Dashboard(const QString& projectDirectory, const QString& engineFile)
    : root(QFileInfo(projectDirectory).absoluteFilePath()),
      engineProgram(engineFile.isEmpty() ? root.filePath("build/log_analyzer") : engineFile) {
    setWindowTitle("Parallel Log Processing & Analytics Engine");
    resize(920, 720);
    buildWindow();
    connect(browse, &QPushButton::clicked, this, &Dashboard::chooseFile);
    connect(files, &QComboBox::currentIndexChanged, this, [this] { resetResults(); });
    connect(workers, &QSpinBox::valueChanged, this, [this](int count) {
        readers->setMaximum(count);
        resetResults();
    });
    connect(readers, &QSpinBox::valueChanged, this, [this] { resetResults(); });
    connect(run, &QPushButton::clicked, this, &Dashboard::startRun);
    connect(exportButton, &QPushButton::clicked, this, &Dashboard::exportCsv);
    connect(&engine, &QProcess::readyReadStandardOutput, this, &Dashboard::readEvents);
    connect(&engine, &QProcess::finished, this, &Dashboard::finishRun);
    connect(&engine, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            busy = false;
            progress->setRange(0, 1000);
            progress->setValue(0);
            status->setText("Cannot start the analyzer. Build it with make and try again.");
            enableControls();
        }
    });
    resetResults();
}

Dashboard::~Dashboard() {
    // Stop our child before the temporary report directory is removed.
    disconnect(&engine, nullptr, this, nullptr);
    if (engine.state() != QProcess::NotRunning) {
        engine.kill();
        engine.waitForFinished(-1);
    }
}

void Dashboard::buildWindow() {
    auto* frame = new QVBoxLayout(this);
    frame->setContentsMargins(0, 0, 0, 0);
    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto* page = new QWidget;
    page->setObjectName("page");
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(20, 16, 20, 16);
    layout->setSpacing(9);
    layout->addWidget(label("CS-2006   /   OPERATING SYSTEMS", "course"));
    layout->addWidget(label("Parallel log analytics", "title"));

    auto* inputRow = new QHBoxLayout;
    inputRow->addWidget(label("Log file"));
    files = new QComboBox;
    files->setObjectName("files");
    files->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    files->addItem("sample.txt  ·  24 lines", root.filePath("data/sample.txt"));
    files->addItem("live-demo.txt  ·  500,000 lines", root.filePath("data/live-demo.txt"));
    files->addItem("repeated-logins.txt  ·  9 lines", root.filePath("data/repeated-logins.txt"));
    inputRow->addWidget(files, 1);
    browse = new QPushButton("Browse…");
    browse->setObjectName("browse");
    inputRow->addWidget(browse);
    layout->addLayout(inputRow);

    auto* settings = new QHBoxLayout;
    workers = new QSpinBox;
    workers->setObjectName("workers");
    workers->setRange(2, 4);
    workers->setValue(4);
    readers = new QSpinBox;
    readers->setObjectName("readers");
    readers->setRange(1, 4);
    readers->setValue(2);
    settings->addWidget(label("Workers"));
    settings->addWidget(workers);
    settings->addSpacing(10);
    settings->addWidget(label("Reader permits"));
    settings->addWidget(readers);
    settings->addStretch();
    run = new QPushButton("Run comparison");
    run->setObjectName("run");
    exportButton = new QPushButton("Export CSV…");
    exportButton->setObjectName("exportCsv");
    settings->addWidget(run);
    settings->addWidget(exportButton);
    layout->addLayout(settings);

    auto* stats = new QHBoxLayout;
    auto stat = [&](const QString& title, const QString& name) {
        auto* box = new QGroupBox(title);
        auto* inside = new QVBoxLayout(box);
        auto* value = label("0", name);
        value->setProperty("metric", true);
        inside->addWidget(value);
        stats->addWidget(box, 1);
        return value;
    };
    processed = stat("Processed lines", "processed");
    failed = stat("Failed-login lines", "failed");
    patterns = stat("Detected patterns", "patternCount");
    layout->addLayout(stats);
    status = label({}, "status");
    status->setWordWrap(true);
    layout->addWidget(status);
    progress = new QProgressBar;
    progress->setObjectName("progress");
    progress->setRange(0, 1000);
    progress->setFixedHeight(12);
    progress->setTextVisible(false);
    layout->addWidget(progress);

    auto* panels = new QGridLayout;
    panels->addWidget(severityPanel(), 0, 0);
    panels->addWidget(workersPanel(), 0, 1);
    panels->addWidget(flagsPanel(), 1, 0);
    panels->addWidget(timingPanel(), 1, 1);
    panels->setColumnStretch(0, 4);
    panels->setColumnStretch(1, 5);
    panels->setRowStretch(0, 1);
    panels->setRowStretch(1, 1);
    layout->addLayout(panels, 1);
    scroll->setWidget(page);
    frame->addWidget(scroll);
    setStyleSheet(R"(
        QWidget#page { background: #f4f7f8; color: #1c333b; }
        QLabel { color: #1c333b; }
        QLabel#course { color: #246d63; font-size: 11px; font-weight: bold; }
        QLabel#title { font-size: 28px; font-weight: bold; }
        QLabel[metric="true"] { font-size: 23px; font-weight: bold; }
        QLabel#status { color: #245e58; font-weight: bold; }
        QGroupBox { background: #fff; color: #20444b; font-weight: bold;
                    border: 1px solid #cbd8da; border-radius: 5px; margin-top: 8px; padding: 8px 4px 3px; }
        QGroupBox::title { subcontrol-origin: margin; left: 9px; padding: 0 4px; }
        QComboBox, QSpinBox, QPushButton { min-height: 24px; }
        QPushButton { padding: 1px 10px; }
        QPushButton#run { background: #11665b; color: white; font-weight: bold; border-radius: 4px; padding: 4px 14px; }
        QPushButton#run:disabled { background: #aac7c2; color: #263d3a; }
        QProgressBar { border: 0; border-radius: 3px; background: #e3e9ea; }
        QProgressBar::chunk { background: #28786d; border-radius: 3px; }
        QTableWidget { border: 0; color: #213943; background: white; alternate-background-color: #f0f5f5; }
        QHeaderView::section { color: #35515a; background: #e8efef; border: 0; padding: 3px; }
    )");
}

QGroupBox* Dashboard::severityPanel() {
    auto* box = new QGroupBox("Severity mix");
    auto* grid = new QGridLayout(box);
    const std::array<QString, 3> colors{"#377ca1", "#a56b1a", "#b54c4d"};
    for (std::size_t i = 0; i < logs::labels.size(); ++i) {
        auto name = QString::fromLatin1(logs::labels[i].data(), int(logs::labels[i].size()));
        grid->addWidget(label(name), int(i), 0);
        severityCounts[i] = label("0", name + "Count");
        severityCounts[i]->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        grid->addWidget(severityCounts[i], int(i), 2);
        if (i < bars.size()) {
            bars[i] = new QProgressBar;
            bars[i]->setObjectName(name + "Bar");
            bars[i]->setRange(0, 1000);
            bars[i]->setTextVisible(false);
            bars[i]->setFixedHeight(12);
            bars[i]->setStyleSheet("QProgressBar::chunk { background: " + colors[i] + "; }");
            grid->addWidget(bars[i], int(i), 1);
        }
    }
    grid->setColumnStretch(1, 1);
    grid->setRowStretch(4, 1);
    grid->setVerticalSpacing(8);
    return box;
}

QGroupBox* Dashboard::workersPanel() {
    auto* box = new QGroupBox("Parallel worker segments");
    auto* layout = new QVBoxLayout(box);
    segments = table({"Worker", "Lines (incl.)", "Bytes [start, end)", "Failures"}, "segments");
    segments->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    segments->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    segments->setMinimumHeight(120);
    layout->addWidget(segments);
    return box;
}

QGroupBox* Dashboard::flagsPanel() {
    auto* box = new QGroupBox("Repeated failed logins");
    auto* layout = new QVBoxLayout(box);
    layout->setSpacing(6);
    auto* counts = new QGridLayout;
    counts->setVerticalSpacing(3);
    flagged = label("0", "flagged");
    unattributed = label("0", "unattributed");
    for (auto* value : {flagged, unattributed}) value->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    counts->addWidget(label("Flagged identities"), 0, 0);
    counts->addWidget(flagged, 0, 1);
    counts->addWidget(label("Failures without an identity"), 1, 0);
    counts->addWidget(unattributed, 1, 1);
    layout->addLayout(counts);
    identities = table({"Identity", "Failures"}, "identities");
    identities->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    identities->setMinimumHeight(78);
    layout->addWidget(identities, 1);
    flagNote = label({}, "flagNote");
    flagNote->setWordWrap(true);
    layout->addWidget(flagNote);
    return box;
}

QGroupBox* Dashboard::timingPanel() {
    auto* box = new QGroupBox("Performance comparison");
    auto* grid = new QGridLayout(box);
    int row = 0;
    auto add = [&](const QString& title, const QString& name) {
        auto* value = label({}, name);
        value->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        grid->addWidget(label(title), row, 0);
        grid->addWidget(value, row++, 1);
        return value;
    };
    indexTime = add("Shared indexing (once)", "indexTime");
    singleTime = add("One worker", "singleTime");
    parallelTime = add("Parallel workers", "parallelTime");
    speedup = add("Speedup", "speedup");
    peak = add("Peak readers / permits", "peak");
    return box;
}

void Dashboard::chooseFile() {
    const auto path = QFileDialog::getOpenFileName(this, "Choose a log file", root.filePath("data"),
                                                  "Text logs (*.txt *.log);;All files (*)");
    if (path.isEmpty()) return;
    const QFileInfo file(path);
    int index = files->findData(file.absoluteFilePath());
    if (index < 0) {
        files->addItem(file.fileName(), file.absoluteFilePath());
        index = files->count() - 1;
    }
    if (files->currentIndex() == index) resetResults();
    else files->setCurrentIndex(index);
}

QString Dashboard::reportPath() const { return cache.filePath("verified.csv"); }

void Dashboard::resetResults() {
    verified = receivedDone = false;
    totalLines = flaggedCount = 0;
    analyzedInput.clear();
    if (cache.isValid()) QFile::remove(reportPath());
    segments->setRowCount(0);
    identities->setRowCount(0);
    for (auto* bar : bars) bar->setValue(0);
    for (auto* value : severityCounts) value->setText("0");
    for (auto* value : {processed, failed, patterns, flagged, unattributed}) value->setText("0");
    for (auto* value : {indexTime, singleTime, parallelTime, speedup, peak}) value->setText("—");
    flagNote->setText("Three failures flag one identity. Three failures without a user or IP raise one file-level alert.");
    progress->setRange(0, 1000);
    progress->setValue(0);
    status->setText("Ready");
    files->setToolTip(files->currentData().toString());
    enableControls();
}

void Dashboard::enableControls() {
    for (QWidget* widget : std::array<QWidget*, 5>{files, browse, workers, readers, run})
        widget->setEnabled(!busy);
    exportButton->setEnabled(!busy && verified);
}

void Dashboard::startRun() {
    if (busy) return;
    resetResults();
    analyzedInput = files->currentData().toString();
    if (!QFileInfo(analyzedInput).isFile() || !QFileInfo(analyzedInput).isReadable()) {
        status->setText("Choose a readable log file.");
        return;
    }
    if (!cache.isValid()) {
        status->setText("Cannot create a temporary report. Check the temporary directory.");
        return;
    }
    busy = true;
    enableControls();
    status->setText("Indexing file…");
    progress->setRange(0, 0);
    engine.start(engineProgram, {analyzedInput, "--threads", QString::number(workers->value()),
                           "--readers", QString::number(readers->value()), "--ui-events",
                           "--report", reportPath()});
}

void Dashboard::readEvents() {
    while (engine.canReadLine()) {
        auto line = engine.readLine();
        if (line.endsWith('\n')) line.chop(1);
        applyEvent(line.split('\t'));
    }
}

void Dashboard::applyEvent(const QList<QByteArray>& f) {
    if (f[0] == "PLAN" && f.size() == 3) {
        totalLines = f[1].toULongLong();
        progress->setRange(0, 1000);
    } else if (f[0] == "INDEX_MS" && f.size() == 2) {
        indexTime->setText(QString::number(f[1].toDouble(), 'f', 3) + " ms");
    } else if (f[0] == "SEGMENT" && f.size() == 6) {
        const int row = segments->rowCount();
        segments->insertRow(row);
        const auto first = f[4].toULongLong(), limit = f[5].toULongLong();
        cell(segments, row, 0, QString::fromLatin1(f[1]));
        cell(segments, row, 1, first == limit ? "Empty" : number(first + 1) + "–" + number(limit));
        cell(segments, row, 2, "[" + QString::fromLatin1(f[2]) + ", " + QString::fromLatin1(f[3]) + ")");
        cell(segments, row, 3, "—");
    } else if (f[0] == "PROGRESS" && f.size() == 10) {
        const auto lines = f[2].toULongLong();
        const auto phase = f[1] == "single" ? QString("Single-worker baseline") : QString("Parallel analysis");
        status->setText(phase + "   ·   " + number(lines) + " / " + number(totalLines) + " lines");
        progress->setValue(perThousand(lines, totalLines));
        processed->setText(number(lines));
        for (std::size_t i = 0; i < severityCounts.size(); ++i) {
            const auto count = f[3 + i].toULongLong();
            severityCounts[i]->setText(number(count));
            if (i < bars.size()) bars[i]->setValue(perThousand(count, lines));
        }
        failed->setText(number(f[7].toULongLong()));
        logs::Counts counts;
        counts.unattributed = f[8].toULongLong();
        counts.patterns = f[9].toULongLong();
        unattributed->setText(number(counts.unattributed) + (counts.has_unattributed_alert() ? " (1 alert)" : ""));
        flaggedCount = counts.patterns;
        flagged->setText(number(flaggedCount));
        patterns->setText(number(counts.total_alerts()));
    } else if (f[0] == "WORKER" && f.size() == 3) {
        const int row = f[1].toInt() - 1;
        if (row >= 0 && row < segments->rowCount()) cell(segments, row, 3, number(f[2].toULongLong()));
    } else if (f[0] == "FLAGGED" && f.size() >= 3) {
        const int row = identities->rowCount();
        identities->insertRow(row);
        // A quoted identity can contain literal tabs between the tag and count.
        cell(identities, row, 0, QString::fromUtf8(f.mid(1, f.size() - 2).join('\t')));
        cell(identities, row, 1, number(f.last().toULongLong()));
    } else if (f[0] == "DONE" && f.size() == 4) {
        const double single = f[1].toDouble(), parallel = f[2].toDouble();
        singleTime->setText(QString::number(single, 'f', 3) + " ms");
        parallelTime->setText(QString::number(parallel, 'f', 3) + " ms");
        speedup->setText(parallel > 0 ? QString::number(single / parallel, 'f', 2) + "×" : "—");
        peak->setText(f[3] + " / " + QString::number(readers->value()));
        receivedDone = true;
    }
}

void Dashboard::finishRun(int code, QProcess::ExitStatus exitStatus) {
    readEvents();
    busy = false;
    const auto error = QString::fromUtf8(engine.readAllStandardError()).trimmed();
    verified = exitStatus == QProcess::NormalExit && code == 0 && receivedDone
            && QFileInfo(reportPath()).size() > 0;
    progress->setRange(0, 1000);
    if (verified) {
        status->setText("Verified · Both runs matched on every count and identity.");
        progress->setValue(1000);
        if (flaggedCount > quint64(identities->rowCount()))
            flagNote->setText("Showing the first 8 flagged identities alphabetically. The CSV includes all "
                              + number(flaggedCount) + ".");
    } else {
        status->setText(error.isEmpty() ? "Analysis ended before results could be verified." : error);
    }
    enableControls();
}

void Dashboard::exportCsv() {
    if (!verified) return;
    QFileDialog dialog(this, "Export verified report");
    dialog.setAcceptMode(QFileDialog::AcceptSave);
    dialog.setNameFilters({"CSV report (*.csv)", "All files (*)"});
    dialog.setDefaultSuffix("csv");
    dialog.selectFile("log-report.csv");
    if (dialog.exec() != QDialog::Accepted || dialog.selectedFiles().isEmpty()) return;
    const auto path = dialog.selectedFiles().first();
    QFile report(reportPath());
    if (!report.open(QIODevice::ReadOnly)) {
        status->setText("Cannot open the verified report. Run the comparison again.");
        return;
    }
    const auto bytes = report.readAll();
    if (report.error() != QFileDevice::NoError) {
        status->setText("Could not read the verified report. Run the comparison again.");
        return;
    }
    try {
        logs::save_report(path.toStdString(), analyzedInput.toStdString(),
                          {bytes.constData(), static_cast<std::size_t>(bytes.size())});
        status->setText("Verified CSV saved to " + path);
    } catch (const std::exception& error) {
        status->setText(QString::fromLocal8Bit(error.what()));
    }
}
