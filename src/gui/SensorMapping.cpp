#include "SensorMapping.h"
#include <QJsonArray>

QJsonObject SensorMapping::toJson() const
{
    QJsonObject obj;
    obj["messagePackId"] = messagePackId;
    obj["nodeIdString"] = nodeIdString;
    obj["typeCode"] = static_cast<int>(typeCode);
    obj["enabled"] = enabled;
    return obj;
}

SensorMapping SensorMapping::fromJson(const QJsonObject &obj)
{
    SensorMapping m;
    m.messagePackId = obj["messagePackId"].toString();
    m.nodeIdString = obj["nodeIdString"].toString();
    m.typeCode = static_cast<quint8>(obj["typeCode"].toInt(0));
    m.enabled = obj["enabled"].toBool(true);
    return m;
}
