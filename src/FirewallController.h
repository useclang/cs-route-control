#pragma once

#include <QObject>

#include <string>
#include <vector>

class FirewallController : public QObject {
    Q_OBJECT

public:
    explicit FirewallController(QObject *parent = nullptr);

    static bool isAdmin();

    void blockRegion(const std::string &regionCode,
                     const std::string &regionName,
                     std::vector<std::string> ips);

    void unblockRegion(const std::string &regionCode,
                       const std::string &regionName,
                       std::vector<std::string> ips);

    void removeAllRules();

signals:
    void operationFinished(QString regionCode, QString regionName, bool blocked, bool success);
    void allRulesRemoved(bool success);
};
