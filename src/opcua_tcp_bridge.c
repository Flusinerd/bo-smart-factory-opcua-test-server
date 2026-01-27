#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/select.h>
#include <sys/time.h>

#include <open62541/client.h>
#include <open62541/client_config_default.h>
#include <open62541/client_highlevel.h>

#define SENSOR_COUNT 4
#define DEFAULT_TCP_PORT 9000
#define UPDATE_INTERVAL_MS 300
#define MAX_CLIENTS 32
#define FRAME_SIZE 18

#define PROTOCOL_VERSION 0x01

typedef struct {
    UA_Boolean values[SENSOR_COUNT];
    UA_UInt64 seq;
    UA_UInt64 timestampMs;
} SensorSnapshot;

typedef struct {
    int socket;
    bool active;
} ClientConnection;

static volatile bool g_running = true;
static ClientConnection g_clients[MAX_CLIENTS];
static int g_listenSocket = -1;

static void
signalHandler(int sig) {
    (void)sig;
    g_running = false;
}

static uint64_t
getTimestampMs(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    uint64_t result = (uint64_t)tv.tv_sec * 1000ULL + (uint64_t)tv.tv_usec / 1000ULL;
    // #region agent log
    FILE *log = fopen("/Users/jan/Dev/opcua-server-hs-bochum/.cursor/debug.log", "a");
    if(log) {
        fprintf(log, "{\"sessionId\":\"debug-session\",\"runId\":\"run1\",\"hypothesisId\":\"A\",\"location\":\"opcua_tcp_bridge.c:52\",\"message\":\"getTimestampMs calculation\",\"data\":{\"tv_sec\":%ld,\"tv_usec\":%ld,\"result_ms\":%llu},\"timestamp\":%llu}\n",
                (long)tv.tv_sec, (long)tv.tv_usec, (unsigned long long)result, (unsigned long long)result);
        fclose(log);
    }
    // #endregion
    return result;
}

static void
writeUint64BE(uint8_t *buf, uint64_t value) {
    buf[0] = (uint8_t)((value >> 56) & 0xFF);
    buf[1] = (uint8_t)((value >> 48) & 0xFF);
    buf[2] = (uint8_t)((value >> 40) & 0xFF);
    buf[3] = (uint8_t)((value >> 32) & 0xFF);
    buf[4] = (uint8_t)((value >> 24) & 0xFF);
    buf[5] = (uint8_t)((value >> 16) & 0xFF);
    buf[6] = (uint8_t)((value >> 8) & 0xFF);
    buf[7] = (uint8_t)(value & 0xFF);
}

static size_t
encodeFrame(const SensorSnapshot *snap, uint8_t *buf, size_t bufSize) {
    if(bufSize < FRAME_SIZE) {
        return 0;
    }

    buf[0] = PROTOCOL_VERSION;
    writeUint64BE(&buf[1], snap->seq);
    writeUint64BE(&buf[9], snap->timestampMs);
    // #region agent log
    FILE *log = fopen("/Users/jan/Dev/opcua-server-hs-bochum/.cursor/debug.log", "a");
    if(log) {
        fprintf(log, "{\"sessionId\":\"debug-session\",\"runId\":\"run1\",\"hypothesisId\":\"B,C\",\"location\":\"opcua_tcp_bridge.c:75\",\"message\":\"After writeUint64BE timestamp\",\"data\":{\"timestampMs\":%llu,\"bytes\":[%u,%u,%u,%u,%u,%u,%u,%u]},\"timestamp\":%llu}\n", 
                (unsigned long long)snap->timestampMs,
                buf[9], buf[10], buf[11], buf[12], buf[13], buf[14], buf[15], buf[16],
                (unsigned long long)getTimestampMs());
        fclose(log);
    }
    // #endregion

    uint8_t flags = 0;
    for(size_t i = 0; i < SENSOR_COUNT; ++i) {
        if(snap->values[i]) {
            flags |= (1U << i);
        }
    }
    buf[17] = flags;

    return FRAME_SIZE;
}

static int
findFreeClientSlot(void) {
    for(int i = 0; i < MAX_CLIENTS; ++i) {
        if(!g_clients[i].active) {
            return i;
        }
    }
    return -1;
}

static void
removeClient(int index) {
    if(index >= 0 && index < MAX_CLIENTS && g_clients[index].active) {
        close(g_clients[index].socket);
        g_clients[index].active = false;
        g_clients[index].socket = -1;
    }
}

static void
acceptNewClient(int listenSocket) {
    struct sockaddr_in clientAddr;
    socklen_t clientLen = sizeof(clientAddr);
    int clientSocket = accept(listenSocket, (struct sockaddr *)&clientAddr, &clientLen);
    
    if(clientSocket < 0) {
        if(errno != EAGAIN && errno != EWOULDBLOCK) {
            fprintf(stderr, "accept() failed: %s\n", strerror(errno));
        }
        return;
    }

    int slot = findFreeClientSlot();
    if(slot < 0) {
        fprintf(stderr, "Max clients reached, rejecting connection\n");
        close(clientSocket);
        return;
    }

    g_clients[slot].socket = clientSocket;
    g_clients[slot].active = true;
    fprintf(stderr, "Client connected from %s:%d (slot %d)\n",
            inet_ntoa(clientAddr.sin_addr), ntohs(clientAddr.sin_port), slot);
}

static void
broadcastFrame(const uint8_t *frame, size_t frameLen) {
    for(int i = 0; i < MAX_CLIENTS; ++i) {
        if(!g_clients[i].active) {
            continue;
        }

        ssize_t sent = send(g_clients[i].socket, frame, frameLen, MSG_NOSIGNAL);
        if(sent < 0 || (size_t)sent != frameLen) {
            fprintf(stderr, "Failed to send to client %d, removing\n", i);
            removeClient(i);
        }
    }
}

static UA_StatusCode
browseToNode(UA_Client *client, const UA_NodeId *startNodeId, 
             UA_UInt16 nsIdx, const char *browseName, UA_NodeClass nodeClass, UA_NodeId *outNodeId) {
    UA_BrowseRequest bReq;
    UA_BrowseRequest_init(&bReq);
    bReq.requestedMaxReferencesPerNode = 0;
    bReq.nodesToBrowse = UA_BrowseDescription_new();
    bReq.nodesToBrowseSize = 1;
    bReq.nodesToBrowse[0].nodeId = *startNodeId;
    bReq.nodesToBrowse[0].browseDirection = UA_BROWSEDIRECTION_FORWARD;
    bReq.nodesToBrowse[0].includeSubtypes = UA_TRUE;
    bReq.nodesToBrowse[0].nodeClassMask = nodeClass;
    bReq.nodesToBrowse[0].resultMask = UA_BROWSERESULTMASK_ALL;

    UA_BrowseResponse bResp = UA_Client_Service_browse(client, bReq);
    UA_StatusCode retval = bResp.responseHeader.serviceResult;
    
    if(retval == UA_STATUSCODE_GOOD && bResp.resultsSize > 0) {
        UA_BrowseResult *result = &bResp.results[0];
        if(result->statusCode == UA_STATUSCODE_GOOD && result->referencesSize > 0) {
            for(size_t i = 0; i < result->referencesSize; ++i) {
                UA_ReferenceDescription *ref = &result->references[i];
                if(ref->browseName.namespaceIndex == nsIdx &&
                   ref->browseName.name.length > 0) {
                    char *name = (char *)UA_malloc(ref->browseName.name.length + 1);
                    if(name) {
                        memcpy(name, ref->browseName.name.data, ref->browseName.name.length);
                        name[ref->browseName.name.length] = '\0';
                        
                        if(strcmp(name, browseName) == 0) {
                            *outNodeId = ref->nodeId.nodeId;
                            UA_free(name);
                            UA_BrowseRequest_clear(&bReq);
                            UA_BrowseResponse_clear(&bResp);
                            return UA_STATUSCODE_GOOD;
                        }
                        UA_free(name);
                    }
                }
            }
        }
    }

    UA_BrowseRequest_clear(&bReq);
    UA_BrowseResponse_clear(&bResp);
    return UA_STATUSCODE_BADNOTFOUND;
}

static UA_StatusCode
findSensorsFolder(UA_Client *client, UA_UInt16 nsIdx, UA_NodeId *outFolderId) {
    UA_NodeId objectsFolder = UA_NODEID_NUMERIC(0, UA_NS0ID_OBJECTSFOLDER);
    return browseToNode(client, &objectsFolder, nsIdx, "Sensors", UA_NODECLASS_OBJECT, outFolderId);
}

static UA_StatusCode
readSensors(UA_Client *client, UA_UInt16 nsIdx, UA_NodeId *sensorNodeIds, SensorSnapshot *out) {
    static UA_UInt64 seqCounter = 0;
    
    out->seq = ++seqCounter;
    out->timestampMs = getTimestampMs();
    // #region agent log
    FILE *log = fopen("/Users/jan/Dev/opcua-server-hs-bochum/.cursor/debug.log", "a");
    if(log) {
        fprintf(log, "{\"sessionId\":\"debug-session\",\"runId\":\"run1\",\"hypothesisId\":\"A\",\"location\":\"opcua_tcp_bridge.c:207\",\"message\":\"Timestamp before encoding\",\"data\":{\"timestampMs\":%llu,\"seq\":%llu},\"timestamp\":%llu}\n", (unsigned long long)out->timestampMs, (unsigned long long)out->seq, (unsigned long long)getTimestampMs());
        fclose(log);
    }
    // #endregion

    for(size_t i = 0; i < SENSOR_COUNT; ++i) {
        UA_Variant value;
        UA_Variant_init(&value);
        
        UA_StatusCode retval = UA_Client_readValueAttribute(client, sensorNodeIds[i], &value);
        if(retval != UA_STATUSCODE_GOOD) {
            UA_Variant_clear(&value);
            fprintf(stderr, "Failed to read Sensor%zu: %s\n", i + 1, UA_StatusCode_name(retval));
            out->values[i] = UA_FALSE;
            continue;
        }

        if(value.type == &UA_TYPES[UA_TYPES_BOOLEAN] && value.data) {
            out->values[i] = *(UA_Boolean *)value.data;
        } else {
            out->values[i] = UA_FALSE;
        }
        
        UA_Variant_clear(&value);
    }

    return UA_STATUSCODE_GOOD;
}

static UA_StatusCode
connectToServer(UA_Client *client, const char *endpointUrl) {
    UA_StatusCode retval = UA_Client_connect(client, endpointUrl);
    if(retval != UA_STATUSCODE_GOOD) {
        fprintf(stderr, "Failed to connect to %s: %s\n", 
                endpointUrl, UA_StatusCode_name(retval));
        return retval;
    }
    
    fprintf(stderr, "Connected to OPC UA server at %s\n", endpointUrl);
    return UA_STATUSCODE_GOOD;
}

static UA_StatusCode
findNamespaceIndex(UA_Client *client, const char *namespaceUri, UA_UInt16 *outNsIdx) {
    UA_String nsUri = UA_STRING_ALLOC(namespaceUri);
    UA_StatusCode retval = UA_Client_NamespaceGetIndex(client, &nsUri, outNsIdx);
    UA_String_clear(&nsUri);
    
    if(retval != UA_STATUSCODE_GOOD) {
        fprintf(stderr, "Namespace '%s' not found: %s\n", namespaceUri, UA_StatusCode_name(retval));
        return retval;
    }
    
    fprintf(stderr, "Found namespace '%s' at index %u\n", namespaceUri, *outNsIdx);
    return UA_STATUSCODE_GOOD;
}

static int
createListenSocket(uint16_t port) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if(sock < 0) {
        fprintf(stderr, "socket() failed: %s\n", strerror(errno));
        return -1;
    }

    int opt = 1;
    if(setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        fprintf(stderr, "setsockopt() failed: %s\n", strerror(errno));
        close(sock);
        return -1;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    if(bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        fprintf(stderr, "bind() failed: %s\n", strerror(errno));
        close(sock);
        return -1;
    }

    if(listen(sock, 5) < 0) {
        fprintf(stderr, "listen() failed: %s\n", strerror(errno));
        close(sock);
        return -1;
    }

    fprintf(stderr, "TCP server listening on port %u\n", port);
    return sock;
}

int main(void) {
    const char *endpointUrl = "opc.tcp://localhost:4840";
    const char *namespaceUri = "urn:binary-sensors-demo";
    uint16_t tcpPort = DEFAULT_TCP_PORT;
    
    const char *portEnv = getenv("OPCUA_TCP_BRIDGE_PORT");
    if(portEnv) {
        int port = atoi(portEnv);
        if(port > 0 && port < 65536) {
            tcpPort = (uint16_t)port;
        }
    }

    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);

    for(int i = 0; i < MAX_CLIENTS; ++i) {
        g_clients[i].active = false;
        g_clients[i].socket = -1;
    }

    g_listenSocket = createListenSocket(tcpPort);
    if(g_listenSocket < 0) {
        return EXIT_FAILURE;
    }

    UA_Client *client = UA_Client_new();
    if(!client) {
        fprintf(stderr, "Failed to create OPC UA client\n");
        close(g_listenSocket);
        return EXIT_FAILURE;
    }

    UA_ClientConfig *config = UA_Client_getConfig(client);
    UA_ClientConfig_setDefault(config);

    UA_UInt16 nsIdx = UA_UINT16_MAX;
    UA_NodeId sensorNodeIds[SENSOR_COUNT];
    bool sensorNodeIdsResolved = false;
    uint64_t lastUpdateMs = 0;
    uint64_t reconnectBackoffMs = 1000;
    uint64_t lastReconnectAttemptMs = 0;

    while(g_running) {
        uint64_t nowMs = getTimestampMs();

        UA_SecureChannelState channelState;
        UA_SessionState sessionState;
        UA_StatusCode connectStatus;
        UA_Client_getState(client, &channelState, &sessionState, &connectStatus);
        bool isConnected = (channelState == UA_SECURECHANNELSTATE_OPEN && 
                           sessionState == UA_SESSIONSTATE_ACTIVATED &&
                           connectStatus == UA_STATUSCODE_GOOD);
        
        if(nsIdx == UA_UINT16_MAX || !isConnected || !sensorNodeIdsResolved) {
            if(nowMs - lastReconnectAttemptMs >= reconnectBackoffMs) {
                UA_Client_disconnect(client);
                UA_StatusCode retval = connectToServer(client, endpointUrl);
                if(retval == UA_STATUSCODE_GOOD) {
                    UA_UInt16 foundNsIdx;
                    retval = findNamespaceIndex(client, namespaceUri, &foundNsIdx);
                    if(retval == UA_STATUSCODE_GOOD) {
                        nsIdx = foundNsIdx;
                        UA_NodeId sensorsFolderId;
                        retval = findSensorsFolder(client, nsIdx, &sensorsFolderId);
                        if(retval == UA_STATUSCODE_GOOD) {
                            sensorNodeIdsResolved = true;
                            for(size_t i = 0; i < SENSOR_COUNT; ++i) {
                                char nameBuf[32];
                                snprintf(nameBuf, sizeof(nameBuf), "Sensor%zu", i + 1);
                                retval = browseToNode(client, &sensorsFolderId, nsIdx, nameBuf, UA_NODECLASS_VARIABLE, &sensorNodeIds[i]);
                                if(retval != UA_STATUSCODE_GOOD) {
                                    fprintf(stderr, "Failed to find %s\n", nameBuf);
                                    sensorNodeIdsResolved = false;
                                    break;
                                }
                            }
                            if(sensorNodeIdsResolved) {
                                fprintf(stderr, "Resolved all sensor node IDs\n");
                                reconnectBackoffMs = 1000;
                            } else {
                                reconnectBackoffMs = (reconnectBackoffMs < 10000) ? 
                                                     reconnectBackoffMs * 2 : 10000;
                            }
                        } else {
                            fprintf(stderr, "Failed to find Sensors folder\n");
                            reconnectBackoffMs = (reconnectBackoffMs < 10000) ? 
                                                 reconnectBackoffMs * 2 : 10000;
                        }
                    } else {
                        reconnectBackoffMs = (reconnectBackoffMs < 10000) ? 
                                             reconnectBackoffMs * 2 : 10000;
                    }
                } else {
                    reconnectBackoffMs = (reconnectBackoffMs < 10000) ? 
                                         reconnectBackoffMs * 2 : 10000;
                }
                lastReconnectAttemptMs = nowMs;
            }
        }

        fd_set readFds;
        FD_ZERO(&readFds);
        FD_SET(g_listenSocket, &readFds);
        
        int maxFd = g_listenSocket;
        for(int i = 0; i < MAX_CLIENTS; ++i) {
            if(g_clients[i].active) {
                FD_SET(g_clients[i].socket, &readFds);
                if(g_clients[i].socket > maxFd) {
                    maxFd = g_clients[i].socket;
                }
            }
        }

        struct timeval timeout;
        timeout.tv_sec = 0;
        timeout.tv_usec = 100000;

        int selectRet = select(maxFd + 1, &readFds, NULL, NULL, &timeout);
        if(selectRet < 0) {
            if(errno == EINTR) {
                continue;
            }
            fprintf(stderr, "select() failed: %s\n", strerror(errno));
            break;
        }

        if(FD_ISSET(g_listenSocket, &readFds)) {
            acceptNewClient(g_listenSocket);
        }

        for(int i = 0; i < MAX_CLIENTS; ++i) {
            if(g_clients[i].active && FD_ISSET(g_clients[i].socket, &readFds)) {
                char buf[1];
                ssize_t recvRet = recv(g_clients[i].socket, buf, sizeof(buf), MSG_PEEK);
                if(recvRet <= 0) {
                    removeClient(i);
                }
            }
        }

        UA_Client_getState(client, &channelState, &sessionState, &connectStatus);
        bool isConnectedNow = (channelState == UA_SECURECHANNELSTATE_OPEN && 
                              sessionState == UA_SESSIONSTATE_ACTIVATED &&
                              connectStatus == UA_STATUSCODE_GOOD);
        
        if(nsIdx != UA_UINT16_MAX && 
           isConnectedNow &&
           sensorNodeIdsResolved &&
           nowMs - lastUpdateMs >= UPDATE_INTERVAL_MS) {
            UA_Client_run_iterate(client, 0);
            
            SensorSnapshot snapshot;
            UA_StatusCode retval = readSensors(client, nsIdx, sensorNodeIds, &snapshot);
            if(retval == UA_STATUSCODE_GOOD) {
                uint8_t frame[FRAME_SIZE];
                size_t frameLen = encodeFrame(&snapshot, frame, sizeof(frame));
                if(frameLen > 0) {
                    // #region agent log
                    FILE *log = fopen("/Users/jan/Dev/opcua-server-hs-bochum/.cursor/debug.log", "a");
                    if(log) {
                        fprintf(log, "{\"sessionId\":\"debug-session\",\"runId\":\"run1\",\"hypothesisId\":\"D\",\"location\":\"opcua_tcp_bridge.c:457\",\"message\":\"Frame before broadcast\",\"data\":{\"timestampBytes\":[%u,%u,%u,%u,%u,%u,%u,%u],\"offset9to16\":\"0x%02x%02x%02x%02x%02x%02x%02x%02x\"},\"timestamp\":%llu}\n",
                                frame[9], frame[10], frame[11], frame[12], frame[13], frame[14], frame[15], frame[16],
                                frame[9], frame[10], frame[11], frame[12], frame[13], frame[14], frame[15], frame[16],
                                (unsigned long long)getTimestampMs());
                        fclose(log);
                    }
                    // #endregion
                    broadcastFrame(frame, frameLen);
                }
            }
            
            lastUpdateMs = nowMs;
        } else if(nsIdx != UA_UINT16_MAX && isConnectedNow) {
            UA_Client_run_iterate(client, 0);
        }
    }

    fprintf(stderr, "Shutting down...\n");

    for(int i = 0; i < MAX_CLIENTS; ++i) {
        removeClient(i);
    }

    if(g_listenSocket >= 0) {
        close(g_listenSocket);
    }

    UA_Client_disconnect(client);
    UA_Client_delete(client);

    return EXIT_SUCCESS;
}
