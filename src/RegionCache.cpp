#include "RegionCache.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>

static QString cachePath() {
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dir);
    return dir + "/regions_cache.json";
}

bool RegionCache::save(const RegionMap &regions) {
    QJsonObject root;
    for (const auto &[code, region] : regions) {
        QJsonObject obj;
        obj["name"] = QString::fromStdString(region.name);
        QJsonArray ips;
        if (region.ips) {
            for (const auto &ip : *region.ips)
                ips.append(QString::fromStdString(ip));
        }
        obj["ips"] = ips;
        root[QString::fromStdString(code)] = obj;
    }

    QFile f(cachePath());
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
    f.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
    return true;
}

bool RegionCache::load(RegionMap &out) {
    QFile f(cachePath());
    if (!f.open(QIODevice::ReadOnly)) return false;

    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    if (doc.isNull() || !doc.isObject()) return false;

    const QJsonObject root = doc.object();
    out.clear();

    for (auto it = root.constBegin(); it != root.constEnd(); ++it) {
        const QJsonObject obj = it.value().toObject();
        ServerRegion region;
        region.code = it.key().toStdString();
        region.name = obj["name"].toString().toStdString();

        auto ips = std::make_shared<std::vector<std::string>>();
        for (const QJsonValue &v : obj["ips"].toArray())
            ips->push_back(v.toString().toStdString());

        if (ips->empty()) continue;
        region.ips = std::move(ips);
        out[region.code] = std::move(region);
    }

    return !out.empty();
}

bool RegionCache::hasCache() {
    return QFile::exists(cachePath());
}
