#ifndef BRIDGEENGINE_H
#define BRIDGEENGINE_H

#include <QObject>
#include <QVector>
#include <QTimer>
#include <QTcpServer>
#include <QTcpSocket>
#include "SensorMapping.h"

struct UA_Client;

class BridgeEngine : public QObject
{
    Q_OBJECT
public:
    explicit BridgeEngine(QObject *parent = nullptr);
    ~BridgeEngine();

    void setEndpointUrl(const QString &url);
    void setNamespaceUri(const QString &uri);
    void setTcpPort(quint16 port);
    void setMappings(const QVector<SensorMapping> &mappings);

    void connectToOpcUa();
    void disconnectFromOpcUa();
    void startBridge();
    void stopBridge();

    bool isOpcUaConnected() const { return m_opcUaConnected; }
    bool isBridgeRunning() const { return m_bridgeRunning; }
    int clientCount() const { return m_clients.size(); }

signals:
    void statusChanged(const QString &status);
    void errorOccurred(const QString &error);
    void clientCountChanged(int count);

private slots:
    void onAcceptConnection();
    void onClientDisconnected();
    void onTick();

private:
    void updateStatus();
    bool resolveNodeIds();
    QByteArray encodeFrame();

    QString m_endpointUrl;
    QString m_namespaceUri;
    quint16 m_tcpPort = 9000;
    QVector<SensorMapping> m_mappings;

    UA_Client *m_client = nullptr;
    QTimer *m_timer = nullptr;
    QTcpServer *m_tcpServer = nullptr;
    QList<QTcpSocket *> m_clients;

    bool m_opcUaConnected = false;
    bool m_bridgeRunning = false;
    quint64 m_sequence = 0;
    quint16 m_namespaceIndex = 0xFFFF;
};

#endif
