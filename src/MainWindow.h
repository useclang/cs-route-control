#pragma once

#include <QLabel>
#include <QMainWindow>
#include <QMouseEvent>
#include <QNetworkAccessManager>
#include <QPoint>
#include <QPointer>
#include <QPushButton>
#include <QTableWidget>
#include <QThreadPool>
#include <QTimer>

#include <atomic>
#include <string>
#include <unordered_map>
#include <vector>

struct ServerRegion {
    std::string              code;
    std::string              name;
    std::vector<std::string> ips;
    int                      ping   = -1;
    int                      jitter = 0;
};

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

    int getPingVersion() const {
        return m_pingVersion.load(std::memory_order_relaxed);
    }

public slots:
    void updatePingDisplay(const std::string &code, int ping);
    void onPingFinished(int version);

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

private slots:
    void loadJson();
    void refreshPings();
    void resetFilter();
    void togglePause();

private:
    void setupUI();
    void populateTable();
    void clearTable();
    void sortTableByPing();

    void parseServerList(const QByteArray &data);

    void showStatus(const QString &msg, int clearAfterMs = 5000);
    void restoreMonitoringStatus();

    bool ensureAdmin();

    QTableWidget *m_table       = nullptr;
    QPushButton  *m_loadButton  = nullptr;
    QPushButton  *m_resetButton = nullptr;
    QPushButton  *m_pauseButton = nullptr;
    QLabel       *m_statusLabel = nullptr;

    QTimer *m_pingTimer    = nullptr;
    QTimer *m_restoreTimer = nullptr;
    QTimer *m_blinkTimer   = nullptr;

    QNetworkAccessManager *m_networkManager = nullptr;

    std::unordered_map<std::string, ServerRegion> m_regions;
    std::unordered_map<std::string, int>          m_codeToRow;

    std::atomic<int> m_pingVersion{0};
    std::atomic<int> m_pendingPings{0};

    QThreadPool m_pingPool;

    std::atomic<bool> m_isPaused{false};
    std::atomic<bool> m_isDownloading{false};
    std::atomic<bool> m_isDestroying{false};

    bool    m_blinkState        = false;
    bool    m_warnedAboutAdmin  = false;
    QString m_lastActionText;

    bool   m_isDragging   = false;
    QPoint m_dragPosition;
};
