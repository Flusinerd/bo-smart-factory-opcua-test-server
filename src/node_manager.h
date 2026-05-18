#ifndef NODE_MANAGER_H
#define NODE_MANAGER_H

#include <open62541/server.h>
#include <pthread.h>
#include <stdbool.h>

#include "server_config.h"

typedef struct {
    UA_NodeId nodeId;
    UA_Boolean value;
    UA_Boolean overridden;
    char browseName[SERVER_BROWSE_NAME_MAX];
} SimulatedSensor;

typedef struct NodeManager NodeManager;

struct NodeManager {
    UA_Server *server;
    UA_UInt16 nsIdx;
    ServerConfig config;
    pthread_mutex_t mutex;
    SimulatedSensor *sensors;
    size_t sensorCount;
    size_t sensorCapacity;
    UA_Boolean simulationEnabled;
    UA_NodeId simulationEnabledNodeId;
    bool simulationNodeSet;
};

NodeManager *node_manager_create(UA_Server *server, UA_UInt16 nsIdx, ServerConfig *cfg);
void node_manager_destroy(NodeManager *nm);

UA_StatusCode node_manager_build(NodeManager *nm);
UA_StatusCode node_manager_add_folder(NodeManager *nm, const char *parentBrowseName,
                                      const char *browseName, const char *displayName);
UA_StatusCode node_manager_add_variable(NodeManager *nm, const char *parentBrowseName,
                                         const char *browseName, const char *displayName,
                                         ServerDataType dataType, bool simulation);
UA_StatusCode node_manager_delete(NodeManager *nm, const char *browseName);

void node_manager_run_simulation_tick(NodeManager *nm);
void node_manager_set_simulation_enabled(NodeManager *nm, UA_Boolean enabled);
void node_manager_reset_overrides(NodeManager *nm);

const ServerConfig *node_manager_get_config(const NodeManager *nm);
ServerConfig *node_manager_get_config_mut(NodeManager *nm);
bool node_manager_save_config(NodeManager *nm);

int node_manager_find_node_index(const NodeManager *nm, const char *browseName);
void node_manager_simulation_callback(UA_Server *server, void *data);

#endif
