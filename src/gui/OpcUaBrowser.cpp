#include "OpcUaBrowser.h"
#include <open62541/client.h>
#include <open62541/client_config_default.h>
#include <open62541/client_highlevel.h>
#include <QByteArray>

OpcUaBrowser::OpcUaBrowser(QObject *parent)
    : QObject(parent)
    , m_client(nullptr)
{
}

OpcUaBrowser::~OpcUaBrowser()
{
    disconnect();
}

QString OpcUaBrowser::nodeIdToString(const UA_NodeId *id) const
{
    if (!id)
        return QString();
    UA_String str = UA_STRING_NULL;
    UA_NodeId_print(id, &str);
    if (str.length == 0)
        return QString();
    QString result = QString::fromUtf8(reinterpret_cast<const char *>(str.data), static_cast<int>(str.length));
    UA_String_clear(&str);
    return result;
}

bool OpcUaBrowser::browseRecursive(const UA_NodeId &parentId, UA_UInt16 nsIdx)
{
    UA_BrowseRequest bReq;
    UA_BrowseRequest_init(&bReq);
    bReq.requestedMaxReferencesPerNode = 0;
    bReq.nodesToBrowse = UA_BrowseDescription_new();
    bReq.nodesToBrowseSize = 1;
    bReq.nodesToBrowse[0].nodeId = parentId;
    bReq.nodesToBrowse[0].browseDirection = UA_BROWSEDIRECTION_FORWARD;
    bReq.nodesToBrowse[0].includeSubtypes = UA_TRUE;
    bReq.nodesToBrowse[0].nodeClassMask = UA_NODECLASS_VARIABLE | UA_NODECLASS_OBJECT;
    bReq.nodesToBrowse[0].resultMask = UA_BROWSERESULTMASK_BROWSENAME | UA_BROWSERESULTMASK_DISPLAYNAME | UA_BROWSERESULTMASK_NODECLASS;

    UA_BrowseResponse bResp = UA_Client_Service_browse(m_client, bReq);

    UA_StatusCode status = bResp.responseHeader.serviceResult;
    if (status != UA_STATUSCODE_GOOD || bResp.resultsSize == 0) {
        UA_BrowseRequest_clear(&bReq);
        UA_BrowseResponse_clear(&bResp);
        return false;
    }

    UA_BrowseResult *result = &bResp.results[0];
    if (result->statusCode != UA_STATUSCODE_GOOD) {
        UA_BrowseRequest_clear(&bReq);
        UA_BrowseResponse_clear(&bResp);
        return false;
    }

    for (size_t i = 0; i < result->referencesSize; ++i) {
        UA_ReferenceDescription *ref = &result->references[i];
        DiscoveredNode node;
        node.nodeClass = ref->nodeClass;
        node.nodeIdString = nodeIdToString(&ref->nodeId.nodeId);

        if (ref->browseName.name.length > 0) {
            node.browseName = QString::fromUtf8(
                reinterpret_cast<const char *>(ref->browseName.name.data),
                static_cast<int>(ref->browseName.name.length));
        }
        if (ref->displayName.text.length > 0) {
            node.displayName = QString::fromUtf8(
                reinterpret_cast<const char *>(ref->displayName.text.data),
                static_cast<int>(ref->displayName.text.length));
        }
        if (node.displayName.isEmpty())
            node.displayName = node.browseName;
        if (node.browseName.isEmpty())
            node.browseName = node.nodeIdString;

        if (ref->nodeClass == UA_NODECLASS_VARIABLE) {
            m_nodes.append(node);
        } else if (ref->nodeClass == UA_NODECLASS_OBJECT) {
            UA_NodeId childId;
            UA_NodeId_copy(&ref->nodeId.nodeId, &childId);
            browseRecursive(childId, nsIdx);
        }
    }

    UA_BrowseRequest_clear(&bReq);
    UA_BrowseResponse_clear(&bResp);
    return true;
}

bool OpcUaBrowser::connectAndBrowse(const QString &endpointUrl, const QString &namespaceUri)
{
    disconnect();
    m_nodes.clear();

    m_client = UA_Client_new();
    if (!m_client) {
        emit finished(false, tr("Failed to create OPC UA client"));
        return false;
    }

    UA_ClientConfig *config = UA_Client_getConfig(m_client);
    UA_ClientConfig_setDefault(config);

    QByteArray urlBytes = endpointUrl.toUtf8();
    UA_StatusCode retval = UA_Client_connect(m_client, urlBytes.constData());
    if (retval != UA_STATUSCODE_GOOD) {
        QString err = QString::fromUtf8(UA_StatusCode_name(retval));
        UA_Client_delete(m_client);
        m_client = nullptr;
        emit finished(false, tr("Failed to connect: %1").arg(err));
        return false;
    }

    UA_UInt16 nsIdx = UA_UINT16_MAX;
    QByteArray nsBytes = namespaceUri.toUtf8();
    UA_String nsUri = UA_STRING(const_cast<char *>(nsBytes.constData()));
    retval = UA_Client_NamespaceGetIndex(m_client, &nsUri, &nsIdx);
    if (retval != UA_STATUSCODE_GOOD) {
        QString err = QString::fromUtf8(UA_StatusCode_name(retval));
        UA_Client_disconnect(m_client);
        UA_Client_delete(m_client);
        m_client = nullptr;
        emit finished(false, tr("Namespace not found: %1").arg(err));
        return false;
    }

    UA_NodeId objectsFolder = UA_NODEID_NUMERIC(0, UA_NS0ID_OBJECTSFOLDER);
    if (!browseRecursive(objectsFolder, nsIdx)) {
        UA_Client_disconnect(m_client);
        UA_Client_delete(m_client);
        m_client = nullptr;
        emit finished(false, tr("Browse failed"));
        return false;
    }

    emit finished(true, QString());
    return true;
}

void OpcUaBrowser::disconnect()
{
    if (m_client) {
        UA_Client_disconnect(m_client);
        UA_Client_delete(m_client);
        m_client = nullptr;
    }
    m_nodes.clear();
}
