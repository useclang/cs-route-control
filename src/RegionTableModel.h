#pragma once

#include "ServerRegion.h"

#include <QAbstractTableModel>

#include <unordered_map>
#include <vector>

class RegionTableModel : public QAbstractTableModel {
    Q_OBJECT

public:
    using RegionMap = std::unordered_map<std::string, ServerRegion>;

    enum Column { ColName = 0, ColPing, ColJitter, ColBlock, ColCount };

    explicit RegionTableModel(QObject *parent = nullptr);

    void setRegions(std::unordered_map<std::string, ServerRegion> regions);
    void updatePingResult(const std::string &code, int ping, double jitter);
    void setBlocked(const std::string &code, bool blocked);
    void clearAllBlocked();

    bool isBlocked(const std::string &code) const;
    const std::unordered_map<std::string, ServerRegion> &regions() const;

    int rowCount(const QModelIndex &parent = {}) const override;
    int columnCount(const QModelIndex &parent = {}) const override;

    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;

    bool setData(const QModelIndex &index, const QVariant &value,
                 int role = Qt::EditRole) override;

signals:
    void blockToggled(std::string code, std::string name,
                      std::vector<std::string> ips, bool blocked);

private:
    struct Row {
        std::string code;
    };

    std::unordered_map<std::string, ServerRegion> m_regions;
    std::unordered_map<std::string, bool>         m_blocked;
    std::vector<Row>                              m_rows;

    int rowForCode(const std::string &code) const;
};
