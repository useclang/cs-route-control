#include "MainWindow.h"
#include "FirewallManager.h"
#include "PingManager.h"
#include "TitleBarWidgets.h"

#include <QApplication>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSettings>
#include <QThreadPool>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

#include <cmath>

static constexpr int kWindowW = 350;
static constexpr int kWindowH = 420;

static const QString kStyleSheet =
    QStringLiteral("QWidget#centralWidget  { background-color: #f7f7f7; "
                   "border: 1px solid #999999; }"
                   "QWidget#titleBar        { background-color: #ffffff; "
                   "border-bottom: 1px solid #dddddd; }"
                   "QPushButton             { outline: none; background-color: "
                   "#ffffff; color: black;"
                   "                          border: 1px solid #ababab; "
                   "border-radius: 2px; padding: 4px; }"
                   "QPushButton:hover       { background-color: #e5e5e5; }"
                   "QPushButton:pressed     { background-color: #d4d4d4; "
                   "border-color: #888888; }"
                   "QHeaderView::section    { background-color: #ffffff; "
                   "color: black; border: none;"
                   "                          border-bottom: 1px solid #ababab;"
                   "                          border-right: 1px solid #e0e0e0; "
                   "padding: 2px 4px; }"
                   "QHeaderView::section:hover   { background-color: #e5e5e5; }"
                   "QHeaderView::section:pressed { background-color: #d4d4d4; }"
                   "QTableWidget            { border: 1px solid #cccccc; "
                   "background-color: #ffffff; }"
                   "QTableWidget::item      { padding: 0px; margin: 0px; }");

static QColor pingColor(int ping) {
  if (ping < 0)
    return QColor(128, 128, 128);
  if (ping < 50)
    return QColor(0, 100, 0);
  if (ping < 100)
    return QColor(204, 204, 0);
  return QColor(139, 0, 0);
}

static QColor jitterColor(int jitter) {
  if (jitter <= 5)
    return QColor(0, 100, 0);
  if (jitter <= 15)
    return QColor(204, 204, 0);
  return QColor(139, 0, 0);
}

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
  m_networkManager = new QNetworkAccessManager(this);

  setupUI();
  ensureAdmin();
  loadJson();

  m_pingTimer = new QTimer(this);
  m_pingTimer->setInterval(2000);
  connect(m_pingTimer, &QTimer::timeout, this, &MainWindow::refreshPings);
  m_pingTimer->start();
}

MainWindow::~MainWindow() { m_pingTimer->stop(); }

void MainWindow::mousePressEvent(QMouseEvent *event) {
  if (event->button() == Qt::LeftButton) {
    m_isDragging = true;
    m_dragPosition =
        event->globalPosition().toPoint() - frameGeometry().topLeft();
    event->accept();
  }
}

void MainWindow::mouseMoveEvent(QMouseEvent *event) {
  if ((event->buttons() & Qt::LeftButton) && m_isDragging) {
    move(event->globalPosition().toPoint() - m_dragPosition);
    event->accept();
  }
}

void MainWindow::mouseReleaseEvent(QMouseEvent *event) {
  m_isDragging = false;
  QMainWindow::mouseReleaseEvent(event);
}

void MainWindow::setupUI() {
  setWindowFlags(Qt::FramelessWindowHint | Qt::WindowSystemMenuHint |
                 Qt::WindowMinimizeButtonHint);
  setAttribute(Qt::WA_TranslucentBackground);
  setFixedSize(kWindowW, kWindowH);
  setStyleSheet(kStyleSheet);

  auto *central = new QWidget(this);
  central->setObjectName("centralWidget");
  setCentralWidget(central);

  auto *mainLayout = new QVBoxLayout(central);
  mainLayout->setContentsMargins(1, 1, 1, 2);
  mainLayout->setSpacing(2);

  auto *titleBar = new QWidget(this);
  titleBar->setObjectName("titleBar");
  titleBar->setFixedHeight(30);

  auto *titleLayout = new QHBoxLayout(titleBar);
  titleLayout->setContentsMargins(10, 0, 0, 0);
  titleLayout->setSpacing(0);

  auto *titleLabel = new QLabel("CS Route Control", titleBar);
  QFont titleFont = titleLabel->font();
  titleFont.setPointSize(9);
  titleLabel->setFont(titleFont);
  titleLabel->setStyleSheet(
      "border: none; color: black; background: transparent;");

  auto *minBtn = new MinButton(titleBar);
  auto *closeBtn = new CloseButton(titleBar);
  connect(minBtn, &QPushButton::clicked, this, &QWidget::showMinimized);
  connect(closeBtn, &QPushButton::clicked, this, &QWidget::close);

  titleLayout->addWidget(titleLabel);
  titleLayout->addStretch();
  titleLayout->addWidget(minBtn);
  titleLayout->addWidget(closeBtn);

  mainLayout->addWidget(titleBar);

  auto *contentLayout = new QVBoxLayout();
  contentLayout->setContentsMargins(4, 2, 4, 0);
  contentLayout->setSpacing(4);

  m_table = new QTableWidget(this);
  m_table->setColumnCount(4);
  m_table->setHorizontalHeaderLabels({"Region", "Ping", "Jitter", "Block"});

  m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
  m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Fixed);
  m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Fixed);
  m_table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Fixed);
  m_table->setColumnWidth(1, 55);
  m_table->setColumnWidth(2, 55);
  m_table->setColumnWidth(3, 45);

  m_table->setShowGrid(false);
  m_table->setAlternatingRowColors(true);
  m_table->verticalHeader()->setVisible(false);
  m_table->verticalHeader()->setDefaultSectionSize(22);
  m_table->horizontalHeader()->setFixedHeight(22);
  m_table->setSortingEnabled(true);
  m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
  m_table->setSelectionMode(QAbstractItemView::NoSelection);

  contentLayout->addWidget(m_table, 1);

  auto *bottomLayout = new QHBoxLayout();
  bottomLayout->setContentsMargins(0, 0, 0, 0);
  bottomLayout->setSpacing(5);

  auto *leftBtnLayout = new QVBoxLayout();
  leftBtnLayout->setSpacing(2);
  leftBtnLayout->setContentsMargins(0, 0, 0, 0);

  m_resetButton = new QPushButton("Reset Firewall", this);
  m_loadButton = new QPushButton("Reload Steam API", this);
  m_resetButton->setFixedSize(110, 26);
  m_loadButton->setFixedSize(110, 26);

  leftBtnLayout->addWidget(m_resetButton);
  leftBtnLayout->addWidget(m_loadButton);

  auto *rightBtnLayout = new QVBoxLayout();
  rightBtnLayout->setSpacing(2);
  rightBtnLayout->setContentsMargins(0, 0, 0, 0);

  m_pauseButton = new QPushButton("Pause", this);
  m_pauseButton->setFixedSize(110, 26);

  rightBtnLayout->addWidget(m_pauseButton);
  rightBtnLayout->addStretch();

  bottomLayout->addLayout(leftBtnLayout);
  bottomLayout->addStretch();
  bottomLayout->addLayout(rightBtnLayout);

  contentLayout->addLayout(bottomLayout, 0);

  m_statusLabel = new QLabel("Starting...", this);
  QFont statusFont = m_statusLabel->font();
  statusFont.setPointSize(8);
  m_statusLabel->setFont(statusFont);
  m_statusLabel->setContentsMargins(2, 0, 2, 1);
  m_statusLabel->setStyleSheet(
      "color: black; border: none; background: transparent;");

  contentLayout->addWidget(m_statusLabel, 0);
  mainLayout->addLayout(contentLayout);

  connect(m_loadButton, &QPushButton::clicked, this, &MainWindow::loadJson);
  connect(m_resetButton, &QPushButton::clicked, this, &MainWindow::resetFilter);
  connect(m_pauseButton, &QPushButton::clicked, this, &MainWindow::togglePause);

  m_blinkTimer = new QTimer(this);
  m_blinkTimer->setInterval(600);
  connect(m_blinkTimer, &QTimer::timeout, this, [this]() {
    m_blinkState = !m_blinkState;
    if (m_isDownloading || !m_lastActionText.isEmpty() || m_isPaused)
      return;
    m_statusLabel->setText(m_blinkState ? "Monitoring..." : "Monitoring..");
  });
  m_blinkTimer->start();

  m_restoreTimer = new QTimer(this);
  m_restoreTimer->setSingleShot(true);
  connect(m_restoreTimer, &QTimer::timeout, this, [this]() {
    m_lastActionText.clear();
    restoreMonitoringStatus();
  });
}

void MainWindow::showStatus(const QString &msg, int clearAfterMs) {
  m_lastActionText = msg;
  m_statusLabel->setText(msg);
  m_restoreTimer->start(clearAfterMs);
}

void MainWindow::restoreMonitoringStatus() {
  if (m_isDownloading)
    return;
  m_statusLabel->setText(
      m_isPaused ? "Paused"
                 : (m_blinkState ? "Monitoring..." : "Monitoring.."));
}

bool MainWindow::ensureAdmin() {
  if (!FirewallManager::isAdmin()) {
    QMessageBox::critical(
        this, "Administrator required",
        "CS Route Control needs to be run as Administrator\n"
        "to add or remove Windows Firewall rules.\n\n"
        "Please restart the application with elevated privileges.");
    return false;
  }
  return true;
}

void MainWindow::togglePause() {
  m_isPaused = !m_isPaused;

  if (m_isPaused) {
    m_pingTimer->stop();
    m_pauseButton->setText("Resume");
    if (m_lastActionText.isEmpty() && !m_isDownloading)
      m_statusLabel->setText("Paused");
  } else {
    m_pingTimer->start();
    m_pauseButton->setText("Pause");
    if (m_lastActionText.isEmpty() && !m_isDownloading)
      restoreMonitoringStatus();
    refreshPings();
  }
}

void MainWindow::loadJson() {
  if (m_isDownloading)
    return;

  m_isDownloading = true;
  m_statusLabel->setText("Downloading server list from Steam...");

  QNetworkRequest request(QUrl(
      "https://api.steampowered.com/ISteamApps/GetSDRConfig/v1/?appid=730"));
  request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                       QNetworkRequest::NoLessSafeRedirectPolicy);

  QNetworkReply *reply = m_networkManager->get(request);

  connect(reply, &QNetworkReply::finished, this, [this, reply]() {
    reply->deleteLater();

    if (reply->error() != QNetworkReply::NoError) {
      QMessageBox::warning(this, "Network Error",
                           "Failed to fetch server list from Steam:\n" +
                               reply->errorString());
      m_isDownloading = false;
      showStatus("Network error — could not load servers.");
      return;
    }

    parseServerList(reply->readAll());
  });
}

void MainWindow::parseServerList(const QByteArray &data) {
  QJsonParseError parseError;
  const QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);

  if (parseError.error != QJsonParseError::NoError) {
    QMessageBox::warning(this, "Parse Error",
                         "Could not parse Steam API response:\n" +
                             parseError.errorString());
    m_isDownloading = false;
    showStatus("JSON parse error.");
    return;
  }

  const QJsonObject root = doc.object();
  if (!root.contains("pops")) {
    QMessageBox::warning(
        this, "API Error",
        "Unexpected response from Steam API: missing 'pops' field.");
    m_isDownloading = false;
    showStatus("API format error.");
    return;
  }

  static const QStringList kAllowedCities = {
      "Amsterdam", "Falkenstein",  "Frankfurt", "Helsinki",    "London",
      "Madrid",    "Paris",        "Stockholm", "Vienna",      "Warsaw",
      "Atlanta",   "Chicago",      "Dallas",    "Los Angeles", "Seattle",
      "Sterling",  "Buenos Aires", "Lima",      "Santiago",    "Sao Paulo",
      "Chennai",   "Mumbai",       "Dubai",     "Hong Kong",   "Seoul",
      "Singapore", "Tokyo",        "Beijing",   "Chengdu",     "Guangdong",
      "Shanghai",  "Johannesburg", "Sydney"};

  m_regions.clear();

  const QJsonObject pops = root.value("pops").toObject();
  for (auto it = pops.constBegin(); it != pops.constEnd(); ++it) {
    const QJsonObject pop = it.value().toObject();
    const QString rawName = pop.value("desc").toString(it.key());

    QString cityName;
    for (const QString &city : kAllowedCities) {
      if (rawName.contains(city, Qt::CaseInsensitive)) {
        cityName = city;
        break;
      }
    }
    if (cityName.isEmpty())
      continue;

    const std::string key = cityName.toStdString();

    if (m_regions.find(key) == m_regions.end()) {
      ServerRegion &r = m_regions[key];
      r.code = key;
      r.name = key;
    }

    const QJsonArray relays = pop.value("relays").toArray();
    for (const QJsonValue &relay : relays) {
      const QString ipv4 = relay.toObject().value("ipv4").toString();
      if (!ipv4.isEmpty())
        m_regions[key].ips.push_back(ipv4.toStdString());
    }
  }

  for (auto mit = m_regions.begin(); mit != m_regions.end();)
    mit = mit->second.ips.empty() ? m_regions.erase(mit) : ++mit;

  populateTable();
  refreshPings();

  m_isDownloading = false;
  m_lastActionText.clear();
  restoreMonitoringStatus();
}

void MainWindow::populateTable() {
  m_table->setSortingEnabled(false);
  clearTable();
  m_table->setRowCount(static_cast<int>(m_regions.size()));

  QSettings settings("CSRouteControl", "Settings");
  int row = 0;

  for (auto &[code, region] : m_regions) {
    m_table->setItem(row, 0,
                     new QTableWidgetItem(QString::fromStdString(region.name)));

    auto *pingItem = new PingTableWidgetItem("...");
    pingItem->setData(Qt::UserRole, 999999);
    pingItem->setTextAlignment(Qt::AlignCenter);
    m_table->setItem(row, 1, pingItem);

    auto *jitterItem = new QTableWidgetItem("-");
    jitterItem->setTextAlignment(Qt::AlignCenter);
    m_table->setItem(row, 2, jitterItem);

    auto *cbWidget = new QWidget();
    auto *cbLayout = new QHBoxLayout(cbWidget);
    cbLayout->setContentsMargins(0, 0, 0, 0);
    cbLayout->setAlignment(Qt::AlignCenter);

    auto *checkBox = new RegionCheckBox();
    cbLayout->addWidget(checkBox);
    m_table->setCellWidget(row, 3, cbWidget);

    const bool savedState =
        settings.value(QString::fromStdString(region.name), false).toBool();
    checkBox->blockSignals(true);
    checkBox->setChecked(savedState);
    checkBox->blockSignals(false);

    const std::vector<std::string> ips = region.ips;
    const std::string regionName = region.name;

    connect(checkBox, &QCheckBox::toggled, this,
            [this, ips, regionName](bool checked) {
              if (!ensureAdmin())
                return;

              bool success = true;
              for (const auto &ip : ips)
                success &= checked ? FirewallManager::addBlockRule(ip)
                                   : FirewallManager::removeBlockRule(ip);

              QSettings cfg("CSRouteControl", "Settings");
              cfg.setValue(QString::fromStdString(regionName), checked);

              showStatus(success ? (checked ? "Blocked: " : "Unblocked: ") +
                                       QString::fromStdString(regionName)
                                 : "Error modifying firewall rules!");
            });

    checkBox->onExclusiveToggle = [this, checkBox, regionName]() {
      if (!ensureAdmin())
        return;

      for (int r = 0; r < m_table->rowCount(); ++r) {
        QWidget *w = m_table->cellWidget(r, 3);
        if (!w)
          continue;
        auto *cb = w->findChild<QCheckBox *>();
        if (cb && cb->isChecked() != (cb != checkBox))
          cb->setChecked(cb != checkBox);
      }

      showStatus("Exclusive mode: " + QString::fromStdString(regionName));
    };

    ++row;
  }

  m_table->setSortingEnabled(true);
}

void MainWindow::clearTable() {
  m_table->clearContents();
  m_table->setRowCount(0);
}

void MainWindow::sortTableByPing() {
  if (m_isPaused)
    return;

  m_table->setUpdatesEnabled(false);
  m_table->sortItems(1, Qt::AscendingOrder);
  m_table->setUpdatesEnabled(true);
}

void MainWindow::refreshPings() {
  if (m_pendingPings > 0 || m_isPaused)
    return;

  const int version = ++m_pingVersion;
  m_pendingPings = static_cast<int>(m_regions.size());

  if (m_pendingPings == 0)
    return;

  for (const auto &[code, region] : m_regions) {
    const std::string codeCopy = code;
    const ServerRegion regionCopy = region;

    QThreadPool::globalInstance()->start(
        [this, version, codeCopy, regionCopy]() {
          int rtt = -1;
          for (const auto &ip : regionCopy.ips) {
            rtt = PingManager::ping(ip);
            if (rtt >= 0 && rtt < 2000)
              break;
          }

          QMetaObject::invokeMethod(
              this,
              [this, version, codeCopy, rtt]() {
                if (version == getPingVersion())
                  updatePingDisplay(codeCopy, rtt);
                onPingFinished();
              },
              Qt::QueuedConnection);
        });
  }
}

void MainWindow::updatePingDisplay(const std::string &code, int ping) {
  auto it = m_regions.find(code);
  if (it == m_regions.end())
    return;

  ServerRegion &region = it->second;
  const int oldPing = region.ping;

  region.ping = ping;
  region.jitter = (oldPing >= 0 && ping >= 0) ? std::abs(ping - oldPing) : 0;

  const QString regionName = QString::fromStdString(region.name);
  for (int row = 0; row < m_table->rowCount(); ++row) {
    if (m_table->item(row, 0)->text() != regionName)
      continue;

    if (auto *pingItem = m_table->item(row, 1)) {
      pingItem->setText(ping >= 0 ? QString::number(ping) + " ms" : "Timeout");
      pingItem->setData(Qt::UserRole, ping >= 0 ? ping : 999999);
      pingItem->setForeground(pingColor(ping));
    }

    if (auto *jitterItem = m_table->item(row, 2)) {
      if (ping >= 0 && oldPing >= 0) {
        jitterItem->setText(QString::number(region.jitter) + " ms");
        jitterItem->setForeground(jitterColor(region.jitter));
      } else {
        jitterItem->setText("-");
        jitterItem->setForeground(QApplication::palette().text().color());
      }
    }

    break;
  }
}

void MainWindow::onPingFinished() {
  if (--m_pendingPings == 0)
    sortTableByPing();
}

void MainWindow::resetFilter() {
  if (!ensureAdmin())
    return;

  for (int row = 0; row < m_table->rowCount(); ++row) {
    QWidget *w = m_table->cellWidget(row, 3);
    if (!w)
      continue;
    auto *cb = w->findChild<QCheckBox *>();
    if (cb && cb->isChecked())
      cb->setChecked(false);
  }

  showStatus("All firewall rules cleared.");
}
