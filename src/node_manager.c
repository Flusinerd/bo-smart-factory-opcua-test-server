#include "node_manager.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SIM_INTERVAL_MS 1000.0
#define NM_MAX_ENTRIES  (SERVER_MAX_NODES + 4)

typedef struct {
    char browseName[SERVER_BROWSE_NAME_MAX];
    UA_NodeId nodeId;
    bool used;
} BrowseMapEntry;

static BrowseMapEntry g_map[NM_MAX_ENTRIES];
static NodeManager *g_nm_for_callbacks = NULL;

static BrowseMapEntry *
map_find(const char *browseName) {
    for(size_t i = 0; i < NM_MAX_ENTRIES; ++i) {
        if(g_map[i].used && strcmp(g_map[i].browseName, browseName) == 0)
            return &g_map[i];
    }
    return NULL;
}

static UA_NodeId
resolve_parent(const char *parentBrowseName) {
    if(!parentBrowseName || !parentBrowseName[0])
        return UA_NODEID_NUMERIC(0, UA_NS0ID_OBJECTSFOLDER);
    BrowseMapEntry *e = map_find(parentBrowseName);
    if(e)
        return e->nodeId;
    return UA_NODEID_NUMERIC(0, UA_NS0ID_OBJECTSFOLDER);
}

static void
map_add(const char *browseName, UA_NodeId nodeId) {
    for(size_t i = 0; i < NM_MAX_ENTRIES; ++i) {
        if(!g_map[i].used) {
            snprintf(g_map[i].browseName, sizeof(g_map[i].browseName), "%s", browseName);
            g_map[i].nodeId = nodeId;
            g_map[i].used = true;
            return;
        }
    }
}

static void
map_remove(const char *browseName) {
    BrowseMapEntry *e = map_find(browseName);
    if(e) {
        UA_NodeId_clear(&e->nodeId);
        e->used = false;
        e->browseName[0] = '\0';
    }
}

static const UA_DataType *
data_type_for(ServerDataType dt) {
    switch(dt) {
    case SERVER_DT_INT32:  return &UA_TYPES[UA_TYPES_INT32];
    case SERVER_DT_DOUBLE: return &UA_TYPES[UA_TYPES_DOUBLE];
    case SERVER_DT_STRING: return &UA_TYPES[UA_TYPES_STRING];
    default:               return &UA_TYPES[UA_TYPES_BOOLEAN];
    }
}

static int
node_depth(const ServerConfig *cfg, const ServerNodeDef *node) {
    int depth = 0;
    char parent[SERVER_BROWSE_NAME_MAX];
    snprintf(parent, sizeof(parent), "%s", node->parentBrowseName);
    for(int guard = 0; guard < 32; ++guard) {
        if(!parent[0])
            return depth;
        bool found = false;
        for(size_t i = 0; i < cfg->nodeCount; ++i) {
            if(strcmp(cfg->nodes[i].browseName, parent) == 0) {
                snprintf(parent, sizeof(parent), "%s", cfg->nodes[i].parentBrowseName);
                depth++;
                found = true;
                break;
            }
        }
        if(!found)
            return depth;
    }
    return depth;
}

static void
sort_nodes_by_depth(const ServerConfig *cfg, ServerNodeDef *sorted, size_t n) {
    for(size_t i = 0; i < n; ++i)
        sorted[i] = cfg->nodes[i];
    for(size_t i = 0; i + 1 < n; ++i) {
        for(size_t j = i + 1; j < n; ++j) {
            if(node_depth(cfg, &sorted[j]) < node_depth(cfg, &sorted[i])) {
                ServerNodeDef tmp = sorted[i];
                sorted[i] = sorted[j];
                sorted[j] = tmp;
            }
        }
    }
}

static bool
add_simulated_sensor(NodeManager *nm, const char *browseName, UA_NodeId nodeId) {
    if(nm->sensorCount >= nm->sensorCapacity) {
        size_t cap = nm->sensorCapacity ? nm->sensorCapacity * 2 : 8;
        SimulatedSensor *s = realloc(nm->sensors, cap * sizeof(SimulatedSensor));
        if(!s)
            return false;
        nm->sensors = s;
        nm->sensorCapacity = cap;
    }
    SimulatedSensor *st = &nm->sensors[nm->sensorCount++];
    st->nodeId = nodeId;
    st->value = UA_FALSE;
    st->overridden = UA_FALSE;
    snprintf(st->browseName, sizeof(st->browseName), "%s", browseName);
    return true;
}

static void
sensor_write_callback(UA_Server *server, const UA_NodeId *sessionId, void *sessionContext,
                      const UA_NodeId *nodeId, void *nodeContext,
                      const UA_NumericRange *range, const UA_DataValue *data) {
    (void)server;
    (void)sessionId;
    (void)sessionContext;
    (void)nodeId;
    (void)range;
    SimulatedSensor *state = (SimulatedSensor *)nodeContext;
    if(!state || !g_nm_for_callbacks)
        return;
    if(!data || !data->hasValue || !data->value.data ||
       data->value.type != &UA_TYPES[UA_TYPES_BOOLEAN])
        return;
    pthread_mutex_lock(&g_nm_for_callbacks->mutex);
    state->value = *(UA_Boolean *)data->value.data;
    state->overridden = UA_TRUE;
    pthread_mutex_unlock(&g_nm_for_callbacks->mutex);
}

static void
simulation_enabled_write_callback(UA_Server *server, const UA_NodeId *sessionId,
                                  void *sessionContext, const UA_NodeId *nodeId,
                                  void *nodeContext, const UA_NumericRange *range,
                                  const UA_DataValue *data) {
    (void)server;
    (void)sessionId;
    (void)sessionContext;
    (void)nodeId;
    (void)nodeContext;
    (void)range;
    NodeManager *nm = g_nm_for_callbacks;
    if(!nm || !data || !data->hasValue || !data->value.data ||
       data->value.type != &UA_TYPES[UA_TYPES_BOOLEAN])
        return;
    pthread_mutex_lock(&nm->mutex);
    nm->simulationEnabled = *(UA_Boolean *)data->value.data;
    UA_Boolean enabled = nm->simulationEnabled;
    size_t sensorCount = nm->sensorCount;
    UA_NodeId sensorIds[SERVER_MAX_NODES];
    for(size_t i = 0; i < sensorCount; ++i) {
        SimulatedSensor *st = &nm->sensors[i];
        if(!enabled) {
            st->value = UA_FALSE;
            st->overridden = UA_FALSE;
        } else {
            st->overridden = UA_FALSE;
        }
        sensorIds[i] = st->nodeId;
    }
    pthread_mutex_unlock(&nm->mutex);

    if(!enabled) {
        for(size_t i = 0; i < sensorCount; ++i) {
            UA_Boolean v = UA_FALSE;
            UA_Variant var;
            UA_Variant_init(&var);
            UA_Variant_setScalar(&var, &v, &UA_TYPES[UA_TYPES_BOOLEAN]);
            UA_Server_writeValue(nm->server, sensorIds[i], var);
        }
    }
}

static UA_StatusCode
create_folder(NodeManager *nm, const ServerNodeDef *def) {
    UA_ObjectAttributes attr = UA_ObjectAttributes_default;
    attr.displayName = UA_LOCALIZEDTEXT_ALLOC("en-US", def->displayName);

    UA_NodeId nodeId;
    UA_NodeId parent = resolve_parent(def->parentBrowseName);
    UA_StatusCode retval = UA_Server_addObjectNode(
        nm->server, UA_NODEID_STRING(nm->nsIdx, def->browseName), parent,
        UA_NODEID_NUMERIC(0, UA_NS0ID_ORGANIZES),
        UA_QUALIFIEDNAME(nm->nsIdx, def->browseName),
        UA_NODEID_NUMERIC(0, UA_NS0ID_FOLDERTYPE), attr, NULL, &nodeId);
    if(retval == UA_STATUSCODE_GOOD)
        map_add(def->browseName, nodeId);
    return retval;
}

static UA_StatusCode
create_variable(NodeManager *nm, const ServerNodeDef *def) {
    const UA_DataType *type = data_type_for(def->dataType);
    UA_VariableAttributes vAttr = UA_VariableAttributes_default;

    UA_Boolean bInit = UA_FALSE;
    UA_Int32 iInit = 0;
    UA_Double dInit = 0.0;
    UA_String sInit = UA_STRING_ALLOC("");

    void *initPtr = &bInit;
    if(def->dataType == SERVER_DT_INT32)
        initPtr = &iInit;
    else if(def->dataType == SERVER_DT_DOUBLE)
        initPtr = &dInit;
    else if(def->dataType == SERVER_DT_STRING)
        initPtr = &sInit;

    UA_Variant_setScalar(&vAttr.value, initPtr, type);
    vAttr.displayName = UA_LOCALIZEDTEXT_ALLOC("en-US", def->displayName);
    vAttr.dataType = type->typeId;
    vAttr.accessLevel = UA_ACCESSLEVELMASK_READ | UA_ACCESSLEVELMASK_WRITE;
    vAttr.userAccessLevel = UA_ACCESSLEVELMASK_READ | UA_ACCESSLEVELMASK_WRITE;

    UA_NodeId nodeId;
    UA_NodeId parent = resolve_parent(def->parentBrowseName);
    UA_StatusCode retval = UA_Server_addVariableNode(
        nm->server, UA_NODEID_STRING(nm->nsIdx, def->browseName), parent,
        UA_NODEID_NUMERIC(0, UA_NS0ID_ORGANIZES),
        UA_QUALIFIEDNAME(nm->nsIdx, def->browseName),
        UA_NODEID_NUMERIC(0, UA_NS0ID_BASEDATAVARIABLETYPE), vAttr, NULL, &nodeId);

    if(def->dataType == SERVER_DT_STRING)
        UA_String_clear(&sInit);

    if(retval != UA_STATUSCODE_GOOD)
        return retval;

    map_add(def->browseName, nodeId);

    if(def->role[0] && strcmp(def->role, SERVER_ROLE_SIMULATION_ENABLED) == 0) {
        nm->simulationEnabledNodeId = nodeId;
        nm->simulationNodeSet = true;
        nm->simulationEnabled = UA_TRUE;
        UA_ValueCallback cb = {NULL, simulation_enabled_write_callback};
        UA_Server_setVariableNode_valueCallback(nm->server, nodeId, cb);
    } else if(def->dataType == SERVER_DT_BOOLEAN && def->simulation) {
        SimulatedSensor *st = NULL;
        if(add_simulated_sensor(nm, def->browseName, nodeId))
            st = &nm->sensors[nm->sensorCount - 1];
        if(st) {
            UA_Server_setNodeContext(nm->server, nodeId, st);
            UA_ValueCallback cb = {NULL, sensor_write_callback};
            UA_Server_setVariableNode_valueCallback(nm->server, nodeId, cb);
        }
    }
    return UA_STATUSCODE_GOOD;
}

NodeManager *
node_manager_create(UA_Server *server, UA_UInt16 nsIdx, ServerConfig *cfg) {
    NodeManager *nm = calloc(1, sizeof(NodeManager));
    if(!nm)
        return NULL;
    nm->server = server;
    nm->nsIdx = nsIdx;
    nm->config = *cfg;
    nm->simulationEnabled = UA_TRUE;
    pthread_mutex_init(&nm->mutex, NULL);
    g_nm_for_callbacks = nm;
    memset(g_map, 0, sizeof(g_map));
    return nm;
}

void
node_manager_destroy(NodeManager *nm) {
    if(!nm)
        return;
    free(nm->sensors);
    for(size_t i = 0; i < NM_MAX_ENTRIES; ++i) {
        if(g_map[i].used)
            UA_NodeId_clear(&g_map[i].nodeId);
    }
    pthread_mutex_destroy(&nm->mutex);
    if(g_nm_for_callbacks == nm)
        g_nm_for_callbacks = NULL;
    free(nm);
}

UA_StatusCode
node_manager_build(NodeManager *nm) {
    ServerNodeDef sorted[SERVER_MAX_NODES];
    size_t n = nm->config.nodeCount;
    if(n > SERVER_MAX_NODES)
        n = SERVER_MAX_NODES;
    sort_nodes_by_depth(&nm->config, sorted, n);

    for(size_t i = 0; i < n; ++i) {
        UA_StatusCode rc;
        if(strcmp(sorted[i].kind, SERVER_NODE_KIND_FOLDER) == 0)
            rc = create_folder(nm, &sorted[i]);
        else if(strcmp(sorted[i].kind, SERVER_NODE_KIND_VARIABLE) == 0)
            rc = create_variable(nm, &sorted[i]);
        else
            continue;
        if(rc != UA_STATUSCODE_GOOD)
            return rc;
    }
    return UA_STATUSCODE_GOOD;
}

static bool
config_has_browse(const ServerConfig *cfg, const char *browseName) {
    for(size_t i = 0; i < cfg->nodeCount; ++i) {
        if(strcmp(cfg->nodes[i].browseName, browseName) == 0)
            return true;
    }
    return false;
}

UA_StatusCode
node_manager_add_folder(NodeManager *nm, const char *parentBrowseName,
                        const char *browseName, const char *displayName) {
    if(!server_config_validate_browse_name(browseName))
        return UA_STATUSCODE_BADINVALIDARGUMENT;
    if(config_has_browse(&nm->config, browseName))
        return UA_STATUSCODE_BADNODEIDEXISTS;

    pthread_mutex_lock(&nm->mutex);
    ServerNodeDef def;
    memset(&def, 0, sizeof(def));
    snprintf(def.kind, sizeof(def.kind), "%s", SERVER_NODE_KIND_FOLDER);
    snprintf(def.browseName, sizeof(def.browseName), "%s", browseName);
    snprintf(def.displayName, sizeof(def.displayName), "%s", displayName);
    snprintf(def.parentBrowseName, sizeof(def.parentBrowseName), "%s",
             parentBrowseName ? parentBrowseName : "");

    UA_StatusCode rc = create_folder(nm, &def);
    if(rc == UA_STATUSCODE_GOOD && nm->config.nodeCount < SERVER_MAX_NODES) {
        nm->config.nodes[nm->config.nodeCount++] = def;
        node_manager_save_config(nm);
    }
    pthread_mutex_unlock(&nm->mutex);
    return rc;
}

UA_StatusCode
node_manager_add_variable(NodeManager *nm, const char *parentBrowseName,
                          const char *browseName, const char *displayName,
                          ServerDataType dataType, bool simulation) {
    if(!server_config_validate_browse_name(browseName))
        return UA_STATUSCODE_BADINVALIDARGUMENT;
    if(config_has_browse(&nm->config, browseName))
        return UA_STATUSCODE_BADNODEIDEXISTS;
    if(simulation && dataType != SERVER_DT_BOOLEAN)
        return UA_STATUSCODE_BADINVALIDARGUMENT;

    pthread_mutex_lock(&nm->mutex);
    ServerNodeDef def;
    memset(&def, 0, sizeof(def));
    snprintf(def.kind, sizeof(def.kind), "%s", SERVER_NODE_KIND_VARIABLE);
    snprintf(def.browseName, sizeof(def.browseName), "%s", browseName);
    snprintf(def.displayName, sizeof(def.displayName), "%s", displayName);
    snprintf(def.parentBrowseName, sizeof(def.parentBrowseName), "%s",
             parentBrowseName ? parentBrowseName : "");
    def.dataType = dataType;
    def.simulation = simulation;

    UA_StatusCode rc = create_variable(nm, &def);
    if(rc == UA_STATUSCODE_GOOD && nm->config.nodeCount < SERVER_MAX_NODES) {
        nm->config.nodes[nm->config.nodeCount++] = def;
        node_manager_save_config(nm);
    }
    pthread_mutex_unlock(&nm->mutex);
    return rc;
}

static bool
has_children_in_config(const ServerConfig *cfg, const char *browseName) {
    for(size_t i = 0; i < cfg->nodeCount; ++i) {
        if(strcmp(cfg->nodes[i].parentBrowseName, browseName) == 0)
            return true;
    }
    return false;
}

UA_StatusCode
node_manager_delete(NodeManager *nm, const char *browseName) {
    pthread_mutex_lock(&nm->mutex);
    int idx = node_manager_find_node_index(nm, browseName);
    if(idx < 0) {
        pthread_mutex_unlock(&nm->mutex);
        return UA_STATUSCODE_BADNODEIDUNKNOWN;
    }
    ServerNodeDef *def = &nm->config.nodes[idx];
    if(def->role[0] && strcmp(def->role, SERVER_ROLE_SIMULATION_ENABLED) == 0) {
        pthread_mutex_unlock(&nm->mutex);
        return UA_STATUSCODE_BADUSERACCESSDENIED;
    }
    if(has_children_in_config(&nm->config, browseName)) {
        pthread_mutex_unlock(&nm->mutex);
        return UA_STATUSCODE_BADNOTIMPLEMENTED;
    }

    BrowseMapEntry *e = map_find(browseName);
    if(e) {
        UA_Server_deleteNode(nm->server, e->nodeId, UA_TRUE);
        map_remove(browseName);
    }

    for(size_t i = 0; i < nm->sensorCount; ++i) {
        if(strcmp(nm->sensors[i].browseName, browseName) == 0) {
            memmove(&nm->sensors[i], &nm->sensors[i + 1],
                    (nm->sensorCount - i - 1) * sizeof(SimulatedSensor));
            nm->sensorCount--;
            break;
        }
    }

    memmove(&nm->config.nodes[idx], &nm->config.nodes[idx + 1],
            (nm->config.nodeCount - (size_t)idx - 1) * sizeof(ServerNodeDef));
    nm->config.nodeCount--;
    node_manager_save_config(nm);
    pthread_mutex_unlock(&nm->mutex);
    return UA_STATUSCODE_GOOD;
}

void
node_manager_run_simulation_tick(NodeManager *nm) {
    pthread_mutex_lock(&nm->mutex);
    if(!nm->simulationEnabled) {
        pthread_mutex_unlock(&nm->mutex);
        return;
    }

    size_t writeCount = 0;
    UA_NodeId writeIds[SERVER_MAX_NODES];
    UA_Boolean writeValues[SERVER_MAX_NODES];
    for(size_t i = 0; i < nm->sensorCount; ++i) {
        SimulatedSensor *st = &nm->sensors[i];
        if(st->overridden)
            continue;
        UA_Boolean newValue = (st->value == UA_FALSE) ? UA_TRUE : UA_FALSE;
        st->value = newValue;
        writeIds[writeCount] = st->nodeId;
        writeValues[writeCount] = newValue;
        writeCount++;
    }
    pthread_mutex_unlock(&nm->mutex);

    for(size_t i = 0; i < writeCount; ++i) {
        UA_Variant v;
        UA_Variant_init(&v);
        UA_Variant_setScalar(&v, &writeValues[i], &UA_TYPES[UA_TYPES_BOOLEAN]);
        UA_Server_writeValue(nm->server, writeIds[i], v);
    }
}

void
node_manager_set_simulation_enabled(NodeManager *nm, UA_Boolean enabled) {
    UA_NodeId simulationNodeId = UA_NODEID_NULL;
    UA_Boolean simulationNodeSet = UA_FALSE;
    size_t sensorCount = 0;
    UA_NodeId sensorIds[SERVER_MAX_NODES];

    pthread_mutex_lock(&nm->mutex);
    nm->simulationEnabled = enabled;
    simulationNodeSet = nm->simulationNodeSet;
    if(simulationNodeSet)
        simulationNodeId = nm->simulationEnabledNodeId;
    sensorCount = nm->sensorCount;
    for(size_t i = 0; i < sensorCount; ++i) {
        SimulatedSensor *st = &nm->sensors[i];
        if(!enabled) {
            st->value = UA_FALSE;
            st->overridden = UA_FALSE;
        } else {
            st->overridden = UA_FALSE;
        }
        sensorIds[i] = st->nodeId;
    }
    pthread_mutex_unlock(&nm->mutex);

    if(simulationNodeSet) {
        UA_Variant v;
        UA_Variant_init(&v);
        UA_Variant_setScalar(&v, &enabled, &UA_TYPES[UA_TYPES_BOOLEAN]);
        UA_Server_writeValue(nm->server, simulationNodeId, v);
    }
    if(!enabled) {
        for(size_t i = 0; i < sensorCount; ++i) {
            UA_Boolean v = UA_FALSE;
            UA_Variant var;
            UA_Variant_init(&var);
            UA_Variant_setScalar(&var, &v, &UA_TYPES[UA_TYPES_BOOLEAN]);
            UA_Server_writeValue(nm->server, sensorIds[i], var);
        }
    }
}

void
node_manager_reset_overrides(NodeManager *nm) {
    pthread_mutex_lock(&nm->mutex);
    for(size_t i = 0; i < nm->sensorCount; ++i)
        nm->sensors[i].overridden = UA_FALSE;
    pthread_mutex_unlock(&nm->mutex);
}

const ServerConfig *
node_manager_get_config(const NodeManager *nm) {
    return &nm->config;
}

ServerConfig *
node_manager_get_config_mut(NodeManager *nm) {
    return &nm->config;
}

bool
node_manager_save_config(NodeManager *nm) {
    return server_config_save(&nm->config);
}

int
node_manager_find_node_index(const NodeManager *nm, const char *browseName) {
    for(size_t i = 0; i < nm->config.nodeCount; ++i) {
        if(strcmp(nm->config.nodes[i].browseName, browseName) == 0)
            return (int)i;
    }
    return -1;
}

void
node_manager_simulation_callback(UA_Server *server, void *data) {
    (void)server;
    NodeManager *nm = (NodeManager *)data;
    if(nm)
        node_manager_run_simulation_tick(nm);
}
