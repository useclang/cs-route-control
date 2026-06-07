#include "RegionTableModel.h"

#include <QApplication>
#include <QColor>
#include <QPalette>

static QColor pingColor(int ping) {
    if (ping < 0)   return QColor(128, 128, 128);
    if (ping < 50)  return QColor(0, 100, 0);
    if (ping < 100) return QColor(204, 204, 0);
    return QColor(139, 0, 0);
}

static QColor jitterColor(double jitter) {
    if (jitter <= 3.0)  return QColor(0, 100, 0);
    if (jitter <= 10.0) return QColor(204, 204, 0);
    return QColor(139, 0, 0);
}

RegionTableModel::RegionTableModel(QObject *parent)
    : QAbstractTableModel(parent) {}

void RegionTableModel::setRegions(std::unordered_map<std::string, ServerRegion> regions) {
    beginResetModel();
    m_regions = std::move(regions);
    m_rows.clear();
    m_rows.reserve(m_regions.size());
    for (const auto &[code, _] : m_regions)
        m_rows.push_back({code});
    endResetModel();
}

void RegionTableModel::updatePingResult(const std::string &code, int ping, double jitter) {
    auto it = m_regions.find(code);
    if (it == m_regions.end()) return;

    it->second.ping   = ping;
    it->second.jitter = jitter;

    const int row = rowForCode(code);
    if (row < 0) return;

    emit dataChanged(index(row, ColPing), index(row, ColJitter),
                     {Qt::DisplayRole, Qt::ForegroundRole, Qt::UserRole});
}

void RegionTableModel::setBlocked(const std::string &code, bool blocked) {
    auto it = m_regions.find(code);
    if (it == m_regions.end()) return;

    m_blocked[code] = blocked;
    const int row = rowForCode(code);
    if (row >= 0)
        emit dataChanged(index(row, ColBlock), index(row, ColBlock), {Qt::CheckStateRole});

    const ServerRegion &region = it->second;
    std::vector<std::string> ips;
    if (region.ips) ips = *region.ips;

    emit blockToggled(code, region.name, std::move(ips), blocked);
}

void RegionTableModel::clearAllBlocked() {
    m_blocked.clear();
    if (!m_rows.empty())
        emit dataChanged(index(0, ColBlock),
                         index(static_cast<int>(m_rows.size()) - 1, ColBlock),
                         {Qt::CheckStateRole});
}

bool RegionTableModel::isBlocked(const std::string &code) const {
    auto it = m_blocked.find(code);
    return it != m_blocked.end() && it->second;
}

const std::unordered_map<std::string, ServerRegion> &RegionTableModel::regions() const {
    return m_regions;
}

int RegionTableModel::rowCount(const QModelIndex &parent) const {
    if (parent.isValid()) return 0;
    return static_cast<int>(m_rows.size());
}

int RegionTableModel::columnCount(const QModelIndex &parent) const {
    if (parent.isValid()) return 0;
    return ColCount;
}

QVariant RegionTableModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid()) return {};
    const int row = index.row();
    if (row < 0 || row >= static_cast<int>(m_rows.size())) return {};

    const std::string &code = m_rows[row].code;
    auto it = m_regions.find(code);
    if (it == m_regions.end()) return {};

    const ServerRegion &region = it->second;

    if (role == Qt::DisplayRole) {
        switch (index.column()) {
        case ColName:
            return QString::fromStdString(region.name);
        case ColPing:
            return region.ping >= 0 ? QString::number(region.ping) + " ms" : "Timeout";
        case ColJitter:
            return region.ping >= 0
                ? QString::number(region.jitter, 'f', 1) + " ms"
                : "-";
        case ColBlock:
            return {};
        }
    }

    if (role == Qt::UserRole) {
        switch (index.column()) {
        case ColPing:
            return region.ping >= 0 ? region.ping : 999999;
        case ColJitter:
            return region.ping >= 0 ? region.jitter : 999999.0;
        default:
            return {};
        }
    }

    if (role == Qt::ForegroundRole) {
        switch (index.column()) {
        case ColPing:
            return pingColor(region.ping);
        case ColJitter:
            return region.ping >= 0
                ? jitterColor(region.jitter)
                : QApplication::palette().text().color();
        default:
            return {};
        }
    }

    if (role == Qt::TextAlignmentRole) {
        switch (index.column()) {
        case ColPing:
        case ColJitter:
            return static_cast<int>(Qt::AlignCenter);
        default:
            return {};
        }
    }

    if (role == Qt::CheckStateRole && index.column() == ColBlock)
        return isBlocked(code) ? Qt::Checked : Qt::Unchecked;

    if (role == Qt::UserRole + 1 && index.column() == ColName)
        return QString::fromStdString(code);

    return {};
}

QVariant RegionTableModel::headerData(int section, Qt::Orientation orientation, int role) const {
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole) return {};
    switch (section) {
    case ColName:   return "Region";
    case ColPing:   return "Ping";
    case ColJitter: return "Jitter";
    case ColBlock:  return "Block";
    }
    return {};
}

Qt::ItemFlags RegionTableModel::flags(const QModelIndex &index) const {
    if (!index.isValid()) return Qt::NoItemFlags;
    Qt::ItemFlags f = Qt::ItemIsEnabled;
    if (index.column() == ColBlock)
        f |= Qt::ItemIsUserCheckable;
    return f;
}

bool RegionTableModel::setData(const QModelIndex &index, const QVariant &value, int role) {
    if (!index.isValid() || role != Qt::CheckStateRole || index.column() != ColBlock)
        return false;

    const int row = index.row();
    if (row < 0 || row >= static_cast<int>(m_rows.size())) return false;

    const std::string &code = m_rows[row].code;
    const bool blocked = (value.toInt() == Qt::Checked);

    setBlocked(code, blocked);
    return true;
}

int RegionTableModel::rowForCode(const std::string &code) const {
    for (int i = 0; i < static_cast<int>(m_rows.size()); ++i)
        if (m_rows[i].code == code) return i;
    return -1;
}
