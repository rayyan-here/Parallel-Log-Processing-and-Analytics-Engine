#pragma once
#include <QDir>
#include <QProcess>
#include <QTemporaryDir>
#include <QWidget>
#include <array>

class QComboBox;
class QGroupBox;
class QLabel;
class QProgressBar;
class QPushButton;
class QSpinBox;
class QTableWidget;

class Dashboard : public QWidget {
public:
    explicit Dashboard(const QString& projectDirectory, const QString& engineFile = {});
    ~Dashboard() override;

private:
    void buildWindow();
    QGroupBox* severityPanel();
    QGroupBox* workersPanel();
    QGroupBox* flagsPanel();
    QGroupBox* timingPanel();
    void chooseFile();
    void resetResults();
    void enableControls();
    void startRun();
    void readEvents();
    void applyEvent(const QList<QByteArray>& fields);
    void finishRun(int exitCode, QProcess::ExitStatus exitStatus);
    void exportCsv();
    QString reportPath() const;

    QDir root;
    QString engineProgram;
    QTemporaryDir cache;
    QProcess engine;
    bool busy = false;
    bool verified = false;
    bool receivedDone = false;
    quint64 totalLines = 0;
    quint64 flaggedCount = 0;
    QString analyzedInput;

    QComboBox* files{};
    QPushButton* browse{};
    QSpinBox* workers{};
    QSpinBox* readers{};
    QPushButton* run{};
    QPushButton* exportButton{};
    QLabel* status{};
    QProgressBar* progress{};
    QLabel* processed{};
    QLabel* failed{};
    QLabel* flagged{};
    QLabel* patterns{};
    std::array<QProgressBar*, 3> bars{};
    std::array<QLabel*, 4> severityCounts{};
    QLabel* unattributed{};
    QTableWidget* segments{};
    QTableWidget* identities{};
    QLabel* flagNote{};
    QLabel* indexTime{};
    QLabel* singleTime{};
    QLabel* parallelTime{};
    QLabel* speedup{};
    QLabel* peak{};
};
