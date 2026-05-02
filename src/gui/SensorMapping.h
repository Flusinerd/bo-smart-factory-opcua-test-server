#ifndef SENSORMAPPING_H
#define SENSORMAPPING_H

#include <QString>
#include <QJsonObject>
#include <open62541/types.h>

struct SensorMapping {
    QString messagePackId;
    QString nodeIdString;
    quint8 typeCode = 0;

    bool enabled = true;
    UA_NodeId nodeId;

    QJsonObject toJson() const;
    static SensorMapping fromJson(const QJsonObject &obj);
};

#endif
