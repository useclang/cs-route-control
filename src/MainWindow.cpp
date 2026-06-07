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
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

static constexpr int kWindowW         = 350;
static constexpr int kWindowH         = 420;
static constexpr int kNetworkTimeoutMs = 10000;

static const QString kStyleSheet = R"(
    QWidget#centralWidget { background-color: #f7f7f7; border: 1px solid #999999; }
    QWidget#titleBar { background-color: #ffffff; border-bottom: 1px solid #dddddd; }
    QPushButton { outline: none; background-color: #ffffff; color: black; border: 1px solid #ababab; border-radius: 2px; padding: 4px; }
    QPushButton:hover { background-color: #e5e5e5; }
    QPushButton:pressed { background-color: #d4d4d4; border-color: #888888; }
    QHeaderView::section { background-color: #ffffff; color: black; border: none; border-bottom: 1px solid #ababab; border-right: 1px solid #e0e0e0; padding: 2px 4px; }
    QHeaderView::section:hover { background-color: #e5e5e5; }
    QHeaderView::section:pressed { background-color: #d4d4d4; }
    QTableWidget { border: 1px solid #cccccc; background-color: #ffffff; }
    QTableWidget::item { padding: 0px; margin: 0px; }
)";

static QColor pingColor(int ping) {
    if (ping < 0)   return QColor(128, 128, 128);
    if (ping < 50)  return QColor(0, 100, 0);
    if (ping < 100) return QColor(204, 204, 0);
    return QColor(139, 0, 0);
}

static QColor jitterColor(int jitter) {
    if (jitter <= 5)  return QColor(0, 100, 0);
    if (jitter <= 15) return QColor(204, 204, 0);
    return QColor(139, 0, 0);
}

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
    m_networkManager = new QNetworkAccessManager(this);
    m_pingPool.setMaxThreadCount(qMax(4, QThread::idealThreadCount()));

    setupUI();
    ensureAdmin();
    loadJson();

    m_pingTimer = new QTimer(this);
    m_pingTimer->setInterval(2000);
    connect(m_pingTimer, &QTimer::timeout, this, &MainWindow::refreshPings);
    m_pingTimer->start();
}

MainWindow::~MainWindow() {
    m_isDestroying = true;
    m_pingTimer->stop();
    ++m_pingVersion;
    m_pingPool.clear();
    m_pingPool.waitForDone();
}

void MainWindow::mousePressEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton) {
        m_isDragging  = true;
        m_dragPosition = event->globalPosition().toPoint() - frameGeometry().topLeft();
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
    setWindowFlags(Qt::FramelessWindowHint | Qt::WindowSystemMenuHint | Qt::WindowMinimizeButtonHint);
    setAttribute(Qt::WA_TranslucentBackground);
    setFixedSize(kWindowW, kWindowH);
    setStyleSheet(kStyleSheet);

    auto *central = new QWidget(this);
    central->setObjectName("centralWidget");
    setCentralWidget(central);

    auto *mainLayout = new QVBoxLayout(central);
    mainLayout->setContentsMargins(1, 1, 1, 2);
    mainLayout->setSpacing(2);

    auto *titleBar = new QWidget(central);
    titleBar->setObjectName("titleBar");
    titleBar->setFixedHeight(30);

    auto *titleLayout = new QHBoxLayout(titleBar);
    titleLayout->setContentsMargins(10, 0, 0, 0);
    titleLayout->setSpacing(0);

    auto *titleLabel = new QLabel("CS Route Control", titleBar);
    QFont titleFont  = titleLabel->font();
    titleFont.setPointSize(9);
    titleLabel->setFont(titleFont);
    titleLabel->setStyleSheet("border: none; color: black; background: transparent;");

    auto *minBtn   = new MinButton(titleBar);
    auto *closeBtn = new CloseButton(titleBar);
    connect(minBtn,   &QPushButton::clicked, this, &QWidget::showMinimized);
    connect(closeBtn, &QPushButton::clicked, this, &QWidget::close);

    titleLayout->addWidget(titleLabel);
    titleLayout->addStretch();
    titleLayout->addWidget(minBtn);
    titleLayout->addWidget(closeBtn);

    mainLayout->addWidget(titleBar);

    auto *contentLayout = new QVBoxLayout();
    contentLayout->setContentsMargins(4, 2, 4, 0);
    contentLayout->setSpacing(4);

    m_table = new QTableWidget(central);
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

    m_resetButton = new QPushButton("Reset Firewall", central);
    m_loadButton  = new QPushButton("Reload Steam API", central);
    m_resetButton->setFixedSize(110, 26);
    m_loadButton->setFixedSize(110, 26);

    leftBtnLayout->addWidget(m_resetButton);
    leftBtnLayout->addWidget(m_loadButton);

    auto *rightBtnLayout = new QVBoxLayout();
    rightBtnLayout->setSpacing(2);
    rightBtnLayout->setContentsMargins(0, 0, 0, 0);

    m_pauseButton = new QPushButton("Pause", central);
    m_pauseButton->setFixedSize(110, 26);

    rightBtnLayout->addWidget(m_pauseButton);
    rightBtnLayout->addStretch();

    bottomLayout->addLayout(leftBtnLayout);
    bottomLayout->addStretch();
    bottomLayout->addLayout(rightBtnLayout);

    contentLayout->addLayout(bottomLayout, 0);

    m_statusLabel = new QLabel("Starting...", central);
    QFont statusFont = m_statusLabel->font();
    statusFont.setPointSize(8);
    m_statusLabel->setFont(statusFont);
    m_statusLabel->setContentsMargins(2, 0, 2, 1);
    m_statusLabel->setStyleSheet("color: black; border: none; background: transparent;");

    contentLayout->addWidget(m_statusLabel, 0);
    mainLayout->addLayout(contentLayout);

    connect(m_loadButton,   &QPushButton::clicked, this, &MainWindow::loadJson);
    connect(m_resetButton,  &QPushButton::clicked, this, &MainWindow::resetFilter);
    connect(m_pauseButton,  &QPushButton::clicked, this, &MainWindow::togglePause);

    m_blinkTimer = new QTimer(this);
    m_blinkTimer->setInterval(600);
    connect(m_blinkTimer, &QTimer::timeout, this, [this]() {
        m_blinkState = !m_blinkState;
        if (m_isDownloading.load() || !m_lastActionText.isEmpty() || m_isPaused.load()) return;
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
    if (m_isDownloading.load()) return;
    m_statusLabel->setText(m_isPaused.load() ? "Paused" : (m_blinkState ? "Monitoring..." : "Monitoring.."));
}

bool MainWindow::ensureAdmin() {
    if (!FirewallManager::isAdmin()) {
        if (!m_warnedAboutAdmin) {
            QMessageBox::critical(this, "Administrator required",
                                  "CS Route Control needs to be run as Administrator\n"
                                  "to add or remove Windows Firewall rules.\n\n"
                                  "Please restart the application with elevated privileges.");
            m_warnedAboutAdmin = true;
        }
        return false;
    }
    return true;
}

void MainWindow::togglePause() {
    const bool nowPaused = !m_isPaused.load();
    m_isPaused.store(nowPaused);

    if (nowPaused) {
        m_pingTimer->stop();
        m_pauseButton->setText("Resume");
        if (m_lastActionText.isEmpty() && !m_isDownloading.load())
            m_statusLabel->setText("Paused");
    } else {
        m_pingTimer->start();
        m_pauseButton->setText("Pause");
        if (m_lastActionText.isEmpty() && !m_isDownloading.load())
            restoreMonitoringStatus();

        ++m_pingVersion;
        refreshPings();
    }
}

void MainWindow::loadJson() {
    if (m_isDownloading.exchange(true)) return;
    showStatus("Fetching server list...");

    QNetworkRequest req(QUrl("https://api.steampowered.com/ISteamApps/GetSDRConfig/v1/?appid=730"));
    req.setHeader(QNetworkRequest::UserAgentHeader, "CS Route Control");
    req.setTransferTimeout(kNetworkTimeoutMs);

    QNetworkReply *reply = m_networkManager->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        m_isDownloading.store(false);
        if (reply->error() != QNetworkReply::NoError) {
            showStatus("Network error: " + reply->errorString());
        } else {
            parseServerList(reply->readAll());
        }
        reply->deleteLater();
    });
}

void MainWindow::parseServerList(const QByteArray &data) {
    const QJsonDocument doc = QJsonDocument::fromJson(data);
    if (doc.isNull()) {
        showStatus("Failed to parse JSON");
        return;
    }

    const QJsonObject pops = doc.object().value("pops").toObject();
    m_regions.clear();

    for (auto it = pops.constBegin(); it != pops.constEnd(); ++it) {
        const QString    &code   = it.key();
        const QJsonObject pop    = it.value().toObject();
        const QJsonArray  relays = pop.value("relays").toArray();
        if (relays.isEmpty()) continue;

        ServerRegion region;
        region.code = code.toStdString();
        region.name = pop.value("desc").toString(code).toStdString();

        for (const QJsonValue &relay : relays) {
            const QString ip = relay.toObject().value("ipv4").toString();
            if (!ip.isEmpty()) region.ips.push_back(ip.toStdString());
        }

        if (!region.ips.empty()) m_regions[region.code] = std::move(region);
    }

    populateTable();
    refreshPings();
    showStatus(QString("Loaded %1 regions").arg(m_regions.size()));
}

void MainWindow::populateTable() {
    m_table->setSortingEnabled(false);
    clearTable();
    m_table->setRowCount(static_cast<int>(m_regions.size()));

    QSettings settings("CSRouteControl", "Settings");
    int row = 0;

    for (auto &[code, region] : m_regions) {
        m_codeToRow[code] = row;

        auto *nameItem = new QTableWidgetItem(QString::fromStdString(region.name));
        nameItem->setData(Qt::UserRole + 1, QString::fromStdString(code));
        m_table->setItem(row, 0, nameItem);

        auto *pingItem = new PingTableWidgetItem("...");
        pingItem->setData(Qt::UserRole, 999999);
        pingItem->setTextAlignment(Qt::AlignCenter);
        m_table->setItem(row, 1, pingItem);

        auto *jitterItem = new QTableWidgetItem("-");
        jitterItem->setTextAlignment(Qt::AlignCenter);
        m_table->setItem(row, 2, jitterItem);

        auto *cbWidget = new QWidget(m_table);
        auto *cbLayout = new QHBoxLayout(cbWidget);
        cbLayout->setContentsMargins(0, 0, 0, 0);
        cbLayout->setAlignment(Qt::AlignCenter);

        auto *checkBox = new RegionCheckBox(cbWidget);
        cbLayout->addWidget(checkBox);
        m_table->setCellWidget(row, 3, cbWidget);

        const QString regionCode = QString::fromStdString(code);
        const QString regionName = QString::fromStdString(region.name);
        const bool savedState    = settings.value(regionCode, false).toBool();

        checkBox->blockSignals(true);
        checkBox->setChecked(savedState);
        checkBox->blockSignals(false);

        const auto ips = std::make_shared<std::vector<std::string>>(region.ips);

        connect(checkBox, &QCheckBox::toggled, this, [this, ips, regionCode, regionName](bool checked) {
            if (!ensureAdmin()) {
                if (auto *cb = qobject_cast<QCheckBox *>(sender())) {
                    cb->blockSignals(true);
                    cb->setChecked(!checked);
                    cb->blockSignals(false);
                }
                return;
            }

            QPointer<MainWindow> self(this);
            QThreadPool::globalInstance()->start([self, ips, checked, regionCode, regionName]() {
                bool success = true;
                for (const auto &ip : *ips)
                    success &= checked ? FirewallManager::addBlockRule(ip) : FirewallManager::removeBlockRule(ip);

                QMetaObject::invokeMethod(self, [self, regionCode, regionName, checked, success]() {
                    if (!self || self->m_isDestroying.load()) return;
                    QSettings cfg("CSRouteControl", "Settings");
                    cfg.setValue(regionCode, checked);
                    self->showStatus(success
                        ? (checked ? QStringLiteral("Blocked: ") : QStringLiteral("Unblocked: ")) + regionName
                        : QStringLiteral("Error modifying firewall rules!"));
                }, Qt::QueuedConnection);
            });
        });

        connect(checkBox, &RegionCheckBox::exclusiveToggleRequested, this, [this, checkBox, regionName]() {
            if (!ensureAdmin()) return;
            for (int r = 0; r < m_table->rowCount(); ++r) {
                QWidget *w = m_table->cellWidget(r, 3);
                if (!w) continue;
                auto *cb = w->findChild<QCheckBox *>();
                if (cb && cb->isChecked() != (cb != checkBox))
                    cb->setChecked(cb != checkBox);
            }
            showStatus("Exclusive mode: " + regionName);
        });

        ++row;
    }

    m_table->setSortingEnabled(true);
}

void MainWindow::clearTable() {
    m_codeToRow.clear();
    m_table->clearContents();
    m_table->setRowCount(0);
}

void MainWindow::sortTableByPing() {
    if (m_isPaused.load()) return;

    m_table->setUpdatesEnabled(false);
    m_table->sortItems(1, Qt::AscendingOrder);
    m_table->setUpdatesEnabled(true);

    m_codeToRow.clear();
    for (int r = 0; r < m_table->rowCount(); ++r) {
        if (auto *item = m_table->item(r, 0)) {
            const QString code = item->data(Qt::UserRole + 1).toString();
            m_codeToRow[code.toStdString()] = r;
        }
    }
}

void MainWindow::refreshPings() {
    if (m_pendingPings.load() > 0 || m_isPaused.load()) return;

    const int version     = ++m_pingVersion;
    const int regionCount = static_cast<int>(m_regions.size());

    if (regionCount == 0) return;
    m_pendingPings.store(regionCount);

    for (const auto &[code, region] : m_regions) {
        const std::string codeCopy = code;
        const auto ips = std::make_shared<std::vector<std::string>>(region.ips);

        QPointer<MainWindow> self(this);
        m_pingPool.start([self, version, codeCopy, ips]() {
            int rtt    = -1;
            int jitter = 0;

            for (const auto &ip : *ips) {
                PingManager::PingResult res = PingManager::ping(ip, 4, 500);
                rtt    = res.avg;
                jitter = res.jitter;
                if (rtt >= 0 && rtt < 2000) break;
            }

            QMetaObject::invokeMethod(self, [self, version, codeCopy, rtt, jitter]() {
                if (!self || self->m_isDestroying.load()) return;
                if (version != self->getPingVersion()) return;

                auto it = self->m_regions.find(codeCopy);
                if (it != self->m_regions.end()) it->second.jitter = jitter;

                self->updatePingDisplay(codeCopy, rtt);
                self->onPingFinished(version);
            }, Qt::QueuedConnection);
        });
    }
}

void MainWindow::updatePingDisplay(const std::string &code, int ping) {
    auto it = m_regions.find(code);
    if (it == m_regions.end()) return;

    ServerRegion &region = it->second;
    region.ping = ping;

    const auto rowIt = m_codeToRow.find(code);
    if (rowIt == m_codeToRow.end()) return;
    const int row = rowIt->second;

    if (auto *pingItem = m_table->item(row, 1)) {
        pingItem->setText(ping >= 0 ? QString::number(ping) + " ms" : "Timeout");
        pingItem->setData(Qt::UserRole, ping >= 0 ? ping : 999999);
        pingItem->setForeground(pingColor(ping));
    }

    if (auto *jitterItem = m_table->item(row, 2)) {
        if (ping >= 0) {
            jitterItem->setText(QString::number(region.jitter) + " ms");
            jitterItem->setForeground(jitterColor(region.jitter));
        } else {
            jitterItem->setText("-");
            jitterItem->setForeground(QApplication::palette().text().color());
        }
    }
}

void MainWindow::onPingFinished(int version) {
    if (version != getPingVersion()) return;
    if (m_pendingPings.fetch_sub(1) == 1) {
        QTimer::singleShot(0, this, [this, version]() {
            if (version == getPingVersion())
                sortTableByPing();
        });
    }
}

void MainWindow::resetFilter() {
    if (!ensureAdmin()) return;

    QPointer<MainWindow> self(this);
    QThreadPool::globalInstance()->start([self]() {
        const bool ok = FirewallManager::removeAllRules();

        QMetaObject::invokeMethod(self, [self, ok]() {
            if (!self || self->m_isDestroying.load()) return;

            QSettings cfg("CSRouteControl", "Settings");
            for (int row = 0; row < self->m_table->rowCount(); ++row) {
                QWidget *w = self->m_table->cellWidget(row, 3);
                if (!w) continue;
                auto *cb = w->findChild<QCheckBox *>();
                if (cb && cb->isChecked()) {
                    cb->blockSignals(true);
                    cb->setChecked(false);
                    cb->blockSignals(false);

                    if (auto *nameItem = self->m_table->item(row, 0)) {
                        const QString code = nameItem->data(Qt::UserRole + 1).toString();
                        cfg.remove(code);
                    }
                }
            }
            self->showStatus(ok ? "All firewall rules cleared." : "Some rules could not be removed.");
        }, Qt::QueuedConnection);
    });
}
