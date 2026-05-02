#ifndef OPCUABROWSER_H
#define OPCUABROWSER_H

#include <QObject>
#include <QVector>
#include <QString>
#include <open62541/types.h>

struct UA_Client;

struct DiscoveredNode {
    QString browseName;
    QString displayName;
    QString nodeIdString;
    UA_NodeClass nodeClass;
};

class OpcUaBrowser : public QObject
{
    Q_OBJECT
public:
    explicit OpcUaBrowser(QObject *parent = nullptr);
    ~OpcUaBrowser();

    bool connectAndBrowse(const QString &endpointUrl, const QString &namespaceUri);
    void disconnect();

    QVector<DiscoveredNode> discoveredNodes() const { return m_nodes; }

signals:
    void finished(bool success, const QString &error);

private:
    bool browseRecursive(const UA_NodeId &parentId, UA_UInt16 nsIdx);
    QString nodeIdToString(const UA_NodeId *id) const;

    UA_Client *m_client = nullptr;
    QVector<DiscoveredNode> m_nodes;
};

#endif
