#pragma once

#include <QMainWindow>
#include <QSortFilterProxyModel>
#include <QNetworkAccessManager>
#include <QLabel>
#include <QTableView>
#include <QPushButton>
#include <QTimer>
#include <QMouseEvent>
#include <atomic>

class RegionTableModel;
class PingScheduler;
class FirewallController;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    bool eventFilter(QObject *obj, QEvent *event) override;

private:
    void setupUI();
    void connectSignals();
    bool ensureAdmin();
    void togglePause();
    void loadJson();
    void parseServerList(const QByteArray &data);
    void resetFilter();
    void showStatus(const QString &msg, int clearAfterMs = 3000);
    void restoreMonitoringStatus();

    RegionTableModel *m_model = nullptr;
    QSortFilterProxyModel *m_proxy = nullptr;
    QNetworkAccessManager *m_networkManager = nullptr;
    PingScheduler *m_pinger = nullptr;
    FirewallController *m_firewall = nullptr;

    QTableView *m_tableView = nullptr;
    QPushButton *m_resetButton = nullptr;
    QPushButton *m_loadButton = nullptr;
    QPushButton *m_pauseButton = nullptr;
    QLabel *m_statusLabel = nullptr;

    QTimer *m_pingTimer = nullptr;
    QTimer *m_blinkTimer = nullptr;
    QTimer *m_restoreTimer = nullptr;

    bool m_isDragging = false;
    QPoint m_dragPosition;
    bool m_blinkState = false;
    bool m_warnedAboutAdmin = false;
    QString m_lastActionText;

    std::atomic<bool> m_isPaused{false};
    std::atomic<bool> m_isDownloading{false};
};
