#pragma once

#include <QLabel>
#include <QMainWindow>
#include <QMouseEvent>
#include <QNetworkAccessManager>
#include <QPoint>
#include <QPushButton>
#include <QTableWidget>
#include <QTimer>

#include <atomic>
#include <map>
#include <string>
#include <vector>

struct ServerRegion {
  std::string code;
  std::string name;
  std::vector<std::string> ips;
  int ping = -1;
  int jitter = 0;
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
  void onPingFinished();

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

  QTableWidget *m_table = nullptr;
  QPushButton *m_loadButton = nullptr;
  QPushButton *m_resetButton = nullptr;
  QPushButton *m_pauseButton = nullptr;
  QLabel *m_statusLabel = nullptr;

  QTimer *m_pingTimer = nullptr;
  QTimer *m_restoreTimer = nullptr;
  QTimer *m_blinkTimer = nullptr;

  QNetworkAccessManager *m_networkManager = nullptr;

  std::map<std::string, ServerRegion> m_regions;

  std::atomic<int> m_pingVersion{0};
  int m_pendingPings = 0;
  bool m_isPaused = false;
  bool m_isDownloading = false;
  bool m_blinkState = false;
  QString m_lastActionText;

  bool m_isDragging = false;
  QPoint m_dragPosition;
};
