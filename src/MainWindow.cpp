#include "MainWindow.h"
#include "FirewallController.h"
#include "PingScheduler.h"
#include "RegionCache.h"
#include "RegionTableModel.h"
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

static constexpr int kWindowW          = 350;
static constexpr int kWindowH          = 420;
static constexpr int kNetworkTimeoutMs = 10000;
static constexpr int kPingIntervalMs   = 2000;

static const QString kStyleSheet = R"(
    QWidget#centralWidget { background-color: #f7f7f7; border: 1px solid #999999; }
    QWidget#titleBar { background-color: #ffffff; border-bottom: 1px solid #dddddd; }
    QPushButton { outline: none; background-color: #ffffff; color: black; border: 1px solid #ababab; border-radius: 2px; padding: 4px; }
    QPushButton:hover { background-color: #e5e5e5; }
    QPushButton:pressed { background-color: #d4d4d4; border-color: #888888; }
    QHeaderView::section { background-color: #ffffff; color: black; border: none; border-bottom: 1px solid #ababab; border-right: 1px solid #e0e0e0; padding: 2px 4px; }
    QHeaderView::section:hover { background-color: #e5e5e5; }
    QHeaderView::section:pressed { background-color: #d4d4d4; }
    QTableView { border: 1px solid #cccccc; background-color: #ffffff; }
    QTableView::item { padding: 0px; margin: 0px; }
)";

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    m_model    = new RegionTableModel(this);
    m_proxy    = new QSortFilterProxyModel(this);
    m_proxy->setSourceModel(m_model);
    m_proxy->setSortRole(Qt::UserRole);

    m_networkManager = new QNetworkAccessManager(this);
    m_pinger         = new PingScheduler(this);
    m_firewall       = new FirewallController(this);

    setupUI();
    connectSignals();
    ensureAdmin();

    if (RegionCache::hasCache()) {
        RegionTableModel::RegionMap cached;
        if (RegionCache::load(cached)) {
            m_model->setRegions(std::move(cached));
            showStatus("Loaded from cache. Fetching update...");
        }
    }

    loadJson();

    m_pingTimer = new QTimer(this);
    m_pingTimer->setInterval(kPingIntervalMs);
    connect(m_pingTimer, &QTimer::timeout, this, [this]() {
        if (!m_isPaused.load())
            m_pinger->schedule(m_model->regions());
    });
    m_pingTimer->start();
}

MainWindow::~MainWindow() {
    m_pingTimer->stop();
    m_pinger->cancel();
}

void MainWindow::mousePressEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton) {
        m_isDragging   = true;
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

bool MainWindow::eventFilter(QObject *obj, QEvent *event) {
    if (obj == m_tableView->viewport() && event->type() == QEvent::MouseButtonPress) {
        auto *mouseEvent = static_cast<QMouseEvent*>(event);
        const bool isAltLeft = (mouseEvent->button() == Qt::LeftButton && (mouseEvent->modifiers() & Qt::AltModifier));
        const bool isMiddle  = (mouseEvent->button() == Qt::MiddleButton);

        if (isAltLeft || isMiddle) {
            QModelIndex proxyIndex = m_tableView->indexAt(mouseEvent->pos());
            if (proxyIndex.isValid()) {
                QString targetCode = m_proxy->data(m_proxy->index(proxyIndex.row(), RegionTableModel::ColName), Qt::UserRole + 1).toString();
                if (!targetCode.isEmpty() && ensureAdmin()) {
                    showStatus("Isolating region...");
                    m_model->blockSignals(true);
                    const auto &allRegions = m_model->regions();
                    for (const auto &[code, region] : allRegions) {
                        bool shouldBlock = (code != targetCode.toStdString());
                        m_model->setBlocked(code, shouldBlock);
                    }
                    m_model->blockSignals(false);
                    m_model->clearAllBlocked();
                    for (const auto &[code, region] : allRegions) {
                        bool shouldBlock = (code != targetCode.toStdString());
                        m_model->setBlocked(code, shouldBlock);
                    }
                    showStatus("Isolated: " + m_proxy->data(m_proxy->index(proxyIndex.row(), RegionTableModel::ColName)).toString());
                    return true;
                }
            }
        }
    }
    return QMainWindow::eventFilter(obj, event);
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

    m_tableView = new QTableView(central);
    m_tableView->setModel(m_proxy);

    m_tableView->horizontalHeader()->setSectionResizeMode(RegionTableModel::ColName,   QHeaderView::Stretch);
    m_tableView->horizontalHeader()->setSectionResizeMode(RegionTableModel::ColPing,   QHeaderView::Fixed);
    m_tableView->horizontalHeader()->setSectionResizeMode(RegionTableModel::ColJitter, QHeaderView::Fixed);
    m_tableView->horizontalHeader()->setSectionResizeMode(RegionTableModel::ColBlock,  QHeaderView::Fixed);
    m_tableView->setColumnWidth(RegionTableModel::ColPing,   55);
    m_tableView->setColumnWidth(RegionTableModel::ColJitter, 60);
    m_tableView->setColumnWidth(RegionTableModel::ColBlock,  45);

    m_tableView->setShowGrid(false);
    m_tableView->setAlternatingRowColors(true);
    m_tableView->verticalHeader()->setVisible(false);
    m_tableView->verticalHeader()->setDefaultSectionSize(22);
    m_tableView->horizontalHeader()->setFixedHeight(22);
    m_tableView->setSortingEnabled(true);
    m_tableView->sortByColumn(RegionTableModel::ColPing, Qt::AscendingOrder);
    m_tableView->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_tableView->setSelectionMode(QAbstractItemView::NoSelection);
    m_tableView->viewport()->installEventFilter(this);

    contentLayout->addWidget(m_tableView, 1);

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

void MainWindow::connectSignals() {
    connect(m_loadButton,   &QPushButton::clicked, this, &MainWindow::loadJson);
    connect(m_resetButton,  &QPushButton::clicked, this, &MainWindow::resetFilter);
    connect(m_pauseButton,  &QPushButton::clicked, this, &MainWindow::togglePause);

    connect(m_pinger, &PingScheduler::resultReady, this,
            [this](PingScheduler::Result r) {
                m_model->updatePingResult(r.code, r.avg, r.jitter);
            });

    connect(m_model, &RegionTableModel::blockToggled, this,
            [this](std::string code, std::string name,
                   std::vector<std::string> ips, bool blocked) {
                if (!ensureAdmin()) {
                    m_model->setBlocked(code, !blocked);
                    return;
                }
                if (blocked)
                    m_firewall->blockRegion(code, name, std::move(ips));
                else
                    m_firewall->unblockRegion(code, name, std::move(ips));
            });

    connect(m_firewall, &FirewallController::operationFinished, this,
            [this](QString code, QString name, bool blocked, bool success) {
                QSettings cfg("CSRouteControl", "Settings");
                if (success) {
                    cfg.setValue(code, blocked);
                    showStatus((blocked ? QStringLiteral("Blocked: ")
                                        : QStringLiteral("Unblocked: ")) + name);
                } else {
                    m_model->setBlocked(code.toStdString(), !blocked);
                    showStatus(QStringLiteral("Error modifying firewall rules!"));
                }
            });

    connect(m_firewall, &FirewallController::allRulesRemoved, this,
            [this](bool ok) {
                m_model->clearAllBlocked();
                QSettings cfg("CSRouteControl", "Settings");
                cfg.clear();
                showStatus(ok ? "All firewall rules cleared."
                              : "Some rules could not be removed.");
            });
}

void MainWindow::showStatus(const QString &msg, int clearAfterMs) {
    m_lastActionText = msg;
    m_statusLabel->setText(msg);
    m_restoreTimer->start(clearAfterMs);
}

void MainWindow::restoreMonitoringStatus() {
    if (m_isDownloading.load()) return;
    m_statusLabel->setText(m_isPaused.load()
        ? "Paused"
        : (m_blinkState ? "Monitoring..." : "Monitoring.."));
}

bool MainWindow::ensureAdmin() {
    if (!FirewallController::isAdmin()) {
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
        m_pinger->cancel();
        m_pauseButton->setText("Resume");
        if (m_lastActionText.isEmpty() && !m_isDownloading.load())
            m_statusLabel->setText("Paused");
    } else {
        m_pinger->cancel();
        m_pauseButton->setText("Pause");
        if (m_lastActionText.isEmpty() && !m_isDownloading.load())
            restoreMonitoringStatus();
        m_pinger->schedule(m_model->regions());
        m_pingTimer->start();
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
    if (pops.isEmpty()) {
        showStatus("No regions found in response");
        return;
    }

    RegionTableModel::RegionMap regions;
    QSettings settings("CSRouteControl", "Settings");

    for (auto it = pops.constBegin(); it != pops.constEnd(); ++it) {
        const QJsonObject pop    = it.value().toObject();
        const QJsonArray  relays = pop.value("relays").toArray();
        if (relays.isEmpty()) continue;

        ServerRegion region;
        region.code = it.key().toStdString();
        region.name = pop.value("desc").toString(it.key()).toStdString();

        auto ips = std::make_shared<std::vector<std::string>>();
        for (const QJsonValue &relay : relays) {
            const QString ip = relay.toObject().value("ipv4").toString();
            if (!ip.isEmpty()) ips->push_back(ip.toStdString());
        }

        if (ips->empty()) continue;
        region.ips = std::move(ips);
        regions[region.code] = std::move(region);
    }

    RegionCache::save(regions);

    m_model->setRegions(std::move(regions));

    for (const auto &[code, region] : m_model->regions()) {
        const QString qCode = QString::fromStdString(code);
        if (settings.value(qCode, false).toBool())
            m_model->setBlocked(code, true);
    }

    showStatus(QString("Loaded %1 regions").arg(m_model->rowCount()));
    m_pinger->schedule(m_model->regions());
}

void MainWindow::resetFilter() {
    if (!ensureAdmin()) return;
    m_firewall->removeAllRules();
}
