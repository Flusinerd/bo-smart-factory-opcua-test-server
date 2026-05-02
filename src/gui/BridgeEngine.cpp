#include "BridgeEngine.h"
#include <open62541/client.h>
#include <open62541/client_config_default.h>
#include <open62541/client_highlevel.h>
#include <msgpack.h>
#include <QRegularExpression>
#include <chrono>

static constexpr quint8 PROTOCOL_VERSION = 0x01;
static constexpr int UPDATE_INTERVAL_MS = 300;

static bool parseNodeId(const QString &str, UA_NodeId *out)
{
    UA_NodeId_init(out);
    QRegularExpression re(R"(ns=(\d+);(s|i|g|b)=(.+))");
    QRegularExpressionMatch match = re.match(str.trimmed());
    if (!match.hasMatch())
        return false;

    bool ok = false;
    quint16 ns = match.captured(1).toUShort(&ok);
    if (!ok)
        return false;

    QString idType = match.captured(2);
    QString idVal = match.captured(3);

    if (idType == QLatin1String("s")) {
        QByteArray buf = idVal.toUtf8();
        *out = UA_NODEID_STRING_ALLOC(ns, buf.constData());
        return true;
    }
    if (idType == QLatin1String("i")) {
        qint32 num = idVal.toInt(&ok);
        if (!ok)
            return false;
        *out = UA_NODEID_NUMERIC(ns, num);
        return true;
    }
    return false;
}

static quint64 timestampMs()
{
    return static_cast<quint64>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
}

BridgeEngine::BridgeEngine(QObject *parent)
    : QObject(parent)
{
    m_timer = new QTimer(this);
    connect(m_timer, &QTimer::timeout, this, &BridgeEngine::onTick);
    m_tcpServer = new QTcpServer(this);
    connect(m_tcpServer, &QTcpServer::newConnection, this, &BridgeEngine::onAcceptConnection);
}

BridgeEngine::~BridgeEngine()
{
    stopBridge();
    disconnectFromOpcUa();
}

void BridgeEngine::setEndpointUrl(const QString &url)
{
    m_endpointUrl = url;
}

void BridgeEngine::setNamespaceUri(const QString &uri)
{
    m_namespaceUri = uri;
}

void BridgeEngine::setTcpPort(quint16 port)
{
    m_tcpPort = port;
}

void BridgeEngine::setMappings(const QVector<SensorMapping> &mappings)
{
    m_mappings = mappings;
    for (auto &m : m_mappings) {
        UA_NodeId_clear(&m.nodeId);
        if (!m.nodeIdString.isEmpty())
            parseNodeId(m.nodeIdString, &m.nodeId);
    }
}

void BridgeEngine::connectToOpcUa()
{
    if (!m_client) {
        m_client = UA_Client_new();
        if (!m_client) {
            emit errorOccurred(tr("Failed to create OPC UA client"));
            return;
        }
        UA_ClientConfig *config = UA_Client_getConfig(m_client);
        UA_ClientConfig_setDefault(config);
    }

    UA_Client_disconnect(m_client);
    QByteArray urlBytes = m_endpointUrl.toUtf8();
    UA_StatusCode retval = UA_Client_connect(m_client, urlBytes.constData());
    if (retval != UA_STATUSCODE_GOOD) {
        m_opcUaConnected = false;
        emit errorOccurred(tr("Connect failed: %1").arg(QString::fromUtf8(UA_StatusCode_name(retval))));
        updateStatus();
        return;
    }

    QByteArray nsBytes = m_namespaceUri.toUtf8();
    UA_String nsUri = UA_STRING(const_cast<char *>(nsBytes.constData()));
    retval = UA_Client_NamespaceGetIndex(m_client, &nsUri, &m_namespaceIndex);
    if (retval != UA_STATUSCODE_GOOD) {
        UA_Client_disconnect(m_client);
        m_opcUaConnected = false;
        emit errorOccurred(tr("Namespace not found: %1").arg(QString::fromUtf8(UA_StatusCode_name(retval))));
        updateStatus();
        return;
    }

    m_opcUaConnected = true;
    if (!resolveNodeIds()) {
        m_opcUaConnected = false;
        emit errorOccurred(tr("Failed to resolve node IDs"));
    }
    updateStatus();
}

void BridgeEngine::disconnectFromOpcUa()
{
    m_opcUaConnected = false;
    if (m_client) {
        UA_Client_disconnect(m_client);
        UA_Client_delete(m_client);
        m_client = nullptr;
    }
    updateStatus();
}

void BridgeEngine::startBridge()
{
    if (m_bridgeRunning)
        return;
    if (!m_client || !m_opcUaConnected) {
        emit errorOccurred(tr("OPC UA not connected"));
        return;
    }

    m_tcpServer->close();
    if (!m_tcpServer->listen(QHostAddress::Any, m_tcpPort)) {
        emit errorOccurred(tr("TCP listen failed: %1").arg(m_tcpServer->errorString()));
        return;
    }

    m_bridgeRunning = true;
    m_timer->start(UPDATE_INTERVAL_MS);
    updateStatus();
    emit statusChanged(tr("Bridge running on port %1").arg(m_tcpPort));
}

void BridgeEngine::stopBridge()
{
    m_bridgeRunning = false;
    m_timer->stop();
    m_tcpServer->close();
    for (QTcpSocket *sock : m_clients) {
        sock->disconnectFromHost();
        sock->deleteLater();
    }
    m_clients.clear();
    emit clientCountChanged(0);
    updateStatus();
}

void BridgeEngine::onAcceptConnection()
{
    while (QTcpSocket *sock = m_tcpServer->nextPendingConnection()) {
        connect(sock, &QTcpSocket::disconnected, this, &BridgeEngine::onClientDisconnected);
        m_clients.append(sock);
        emit clientCountChanged(m_clients.size());
    }
}

void BridgeEngine::onClientDisconnected()
{
    QTcpSocket *sock = qobject_cast<QTcpSocket *>(sender());
    if (sock) {
        m_clients.removeAll(sock);
        sock->deleteLater();
        emit clientCountChanged(m_clients.size());
    }
}

bool BridgeEngine::resolveNodeIds()
{
    for (auto &m : m_mappings) {
        UA_NodeId_clear(&m.nodeId);
        if (!m.nodeIdString.isEmpty() && !parseNodeId(m.nodeIdString, &m.nodeId)) {
            return false;
        }
    }
    return true;
}

QByteArray BridgeEngine::encodeFrame()
{
    msgpack_sbuffer sbuf;
    msgpack_sbuffer_init(&sbuf);
    msgpack_packer pk;
    msgpack_packer_init(&pk, &sbuf, msgpack_sbuffer_write);

    msgpack_pack_map(&pk, 4);

    msgpack_pack_str(&pk, 7);
    msgpack_pack_str_body(&pk, "version", 7);
    msgpack_pack_uint8(&pk, PROTOCOL_VERSION);

    msgpack_pack_str(&pk, 8);
    msgpack_pack_str_body(&pk, "sequence", 8);
    msgpack_pack_uint64(&pk, ++m_sequence);

    msgpack_pack_str(&pk, 11);
    msgpack_pack_str_body(&pk, "timestampMs", 11);
    msgpack_pack_uint64(&pk, timestampMs());

    msgpack_pack_str(&pk, 7);
    msgpack_pack_str_body(&pk, "sensors", 7);

    QVector<SensorMapping> enabled;
    for (const auto &m : m_mappings) {
        if (m.enabled && !m.nodeIdString.isEmpty())
            enabled.append(m);
    }
    msgpack_pack_array(&pk, static_cast<uint32_t>(enabled.size()));

    for (const auto &m : enabled) {
        UA_Variant value;
        UA_Variant_init(&value);
        UA_StatusCode ret = UA_Client_readValueAttribute(m_client, m.nodeId, &value);

        QByteArray msgId = m.messagePackId.isEmpty() ? m.nodeIdString.toUtf8() : m.messagePackId.toUtf8();
        quint8 typeCode = m.typeCode;

        msgpack_pack_map(&pk, 3);
        msgpack_pack_str(&pk, 2);
        msgpack_pack_str_body(&pk, "id", 2);
        msgpack_pack_str(&pk, static_cast<uint32_t>(msgId.size()));
        msgpack_pack_str_body(&pk, msgId.constData(), static_cast<size_t>(msgId.size()));

        msgpack_pack_str(&pk, 4);
        msgpack_pack_str_body(&pk, "type", 4);
        msgpack_pack_uint8(&pk, typeCode);

        msgpack_pack_str(&pk, 5);
        msgpack_pack_str_body(&pk, "value", 5);

        if (ret == UA_STATUSCODE_GOOD && value.data && value.type) {
            if (value.type == &UA_TYPES[UA_TYPES_BOOLEAN]) {
                if (*(UA_Boolean *)value.data)
                    msgpack_pack_true(&pk);
                else
                    msgpack_pack_false(&pk);
            } else if (value.type == &UA_TYPES[UA_TYPES_INT32]) {
                msgpack_pack_int32(&pk, *(UA_Int32 *)value.data);
            } else if (value.type == &UA_TYPES[UA_TYPES_INT64]) {
                msgpack_pack_int64(&pk, *(UA_Int64 *)value.data);
            } else if (value.type == &UA_TYPES[UA_TYPES_FLOAT]) {
                msgpack_pack_float(&pk, *(UA_Float *)value.data);
            } else if (value.type == &UA_TYPES[UA_TYPES_DOUBLE]) {
                msgpack_pack_double(&pk, *(UA_Double *)value.data);
            } else {
                msgpack_pack_false(&pk);
            }
        } else {
            msgpack_pack_false(&pk);
        }
        UA_Variant_clear(&value);
    }

    QByteArray result(sbuf.data, static_cast<int>(sbuf.size));
    msgpack_sbuffer_destroy(&sbuf);
    return result;
}

void BridgeEngine::onTick()
{
    if (!m_client || !m_opcUaConnected || !m_bridgeRunning)
        return;

    UA_Client_run_iterate(m_client, 0);

    UA_SecureChannelState chState;
    UA_SessionState sessState;
    UA_StatusCode connStatus;
    UA_Client_getState(m_client, &chState, &sessState, &connStatus);
    if (chState != UA_SECURECHANNELSTATE_OPEN || sessState != UA_SESSIONSTATE_ACTIVATED || connStatus != UA_STATUSCODE_GOOD) {
        m_opcUaConnected = false;
        emit errorOccurred(tr("OPC UA connection lost"));
        updateStatus();
        return;
    }

    QByteArray frame = encodeFrame();
    if (frame.isEmpty())
        return;

    for (QTcpSocket *sock : m_clients) {
        if (sock->state() == QAbstractSocket::ConnectedState) {
            sock->write(frame);
        }
    }
}

void BridgeEngine::updateStatus()
{
    if (m_bridgeRunning) {
        emit statusChanged(tr("Bridge running | %1 client(s)").arg(m_clients.size()));
    } else if (m_opcUaConnected) {
        emit statusChanged(tr("OPC UA connected"));
    } else {
        emit statusChanged(tr("Disconnected"));
    }
}
