#include "server_config.h"

#include <cJSON.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static char g_configPath[SERVER_CONFIG_PATH_MAX] = "./server-config.json";

void
server_config_init_path(void) {
    const char *env = getenv("OPCUA_SERVER_CONFIG_PATH");
    if(env && env[0]) {
        snprintf(g_configPath, sizeof(g_configPath), "%s", env);
    }
}

const char *
server_config_get_path(void) {
    return g_configPath;
}

static void
add_node(ServerConfig *cfg, const char *kind, const char *browseName,
         const char *displayName, const char *parent, ServerDataType dt,
         bool simulation, const char *role) {
    if(cfg->nodeCount >= SERVER_MAX_NODES)
        return;
    ServerNodeDef *n = &cfg->nodes[cfg->nodeCount++];
    snprintf(n->kind, sizeof(n->kind), "%s", kind);
    snprintf(n->browseName, sizeof(n->browseName), "%s", browseName);
    snprintf(n->displayName, sizeof(n->displayName), "%s", displayName);
    snprintf(n->parentBrowseName, sizeof(n->parentBrowseName), "%s", parent ? parent : "");
    n->dataType = dt;
    n->simulation = simulation;
    if(role)
        snprintf(n->role, sizeof(n->role), "%s", role);
    else
        n->role[0] = '\0';
}

void
server_config_seed_defaults(ServerConfig *cfg) {
    memset(cfg, 0, sizeof(*cfg));
    snprintf(cfg->namespaceUri, sizeof(cfg->namespaceUri), "%s",
             "urn:binary-sensors-demo");
    snprintf(cfg->configPath, sizeof(cfg->configPath), "%s", g_configPath);
    cfg->nodeCount = 0;

    add_node(cfg, SERVER_NODE_KIND_FOLDER, "Sensors", "Sensors", "", SERVER_DT_BOOLEAN,
             false, NULL);
    for(int i = 1; i <= 4; ++i) {
        char bn[32], dn[32];
        snprintf(bn, sizeof(bn), "Sensor%d", i);
        snprintf(dn, sizeof(dn), "Sensor %d", i);
        add_node(cfg, SERVER_NODE_KIND_VARIABLE, bn, dn, "Sensors", SERVER_DT_BOOLEAN,
                 true, NULL);
    }
    add_node(cfg, SERVER_NODE_KIND_VARIABLE, "Pi1_InductionSwitch1",
             "Induction Switch 1 (Pi 1)", "Sensors", SERVER_DT_BOOLEAN, false, NULL);
    add_node(cfg, SERVER_NODE_KIND_VARIABLE, "SimulationEnabled",
             "Simulation Enabled", "Sensors", SERVER_DT_BOOLEAN, false,
             SERVER_ROLE_SIMULATION_ENABLED);
}

const char *
server_config_resolve_namespace_uri(const ServerConfig *cfg) {
    const char *env = getenv("OPCUA_NAMESPACE_URI");
    if(env && env[0])
        return env;
    if(cfg && cfg->namespaceUri[0])
        return cfg->namespaceUri;
    return "urn:binary-sensors-demo";
}

bool
server_config_validate_browse_name(const char *name) {
    if(!name || !name[0])
        return false;
    size_t len = strlen(name);
    if(len >= SERVER_BROWSE_NAME_MAX)
        return false;
    for(size_t i = 0; i < len; ++i) {
        char c = name[i];
        if(!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
             (c >= '0' && c <= '9') || c == '_'))
            return false;
    }
    return true;
}

const char *
server_data_type_to_string(ServerDataType dt) {
    switch(dt) {
    case SERVER_DT_BOOLEAN: return "Boolean";
    case SERVER_DT_INT32:   return "Int32";
    case SERVER_DT_DOUBLE:  return "Double";
    case SERVER_DT_STRING:  return "String";
    default:                return "Boolean";
    }
}

bool
server_data_type_from_string(const char *s, ServerDataType *out) {
    if(!s || !out)
        return false;
    if(strcmp(s, "Boolean") == 0) { *out = SERVER_DT_BOOLEAN; return true; }
    if(strcmp(s, "Int32") == 0)   { *out = SERVER_DT_INT32;   return true; }
    if(strcmp(s, "Double") == 0)  { *out = SERVER_DT_DOUBLE;  return true; }
    if(strcmp(s, "String") == 0)  { *out = SERVER_DT_STRING;  return true; }
    return false;
}

static bool
parse_node(const cJSON *item, ServerNodeDef *n) {
    const cJSON *kind = cJSON_GetObjectItemCaseSensitive(item, "kind");
    const cJSON *browse = cJSON_GetObjectItemCaseSensitive(item, "browseName");
    const cJSON *display = cJSON_GetObjectItemCaseSensitive(item, "displayName");
    const cJSON *parent = cJSON_GetObjectItemCaseSensitive(item, "parentBrowseName");
    const cJSON *dtype = cJSON_GetObjectItemCaseSensitive(item, "dataType");
    const cJSON *sim = cJSON_GetObjectItemCaseSensitive(item, "simulation");
    const cJSON *role = cJSON_GetObjectItemCaseSensitive(item, "role");

    if(!cJSON_IsString(kind) || !cJSON_IsString(browse) || !cJSON_IsString(display))
        return false;

    memset(n, 0, sizeof(*n));
    snprintf(n->kind, sizeof(n->kind), "%s", kind->valuestring);
    snprintf(n->browseName, sizeof(n->browseName), "%s", browse->valuestring);
    snprintf(n->displayName, sizeof(n->displayName), "%s", display->valuestring);
    if(cJSON_IsString(parent))
        snprintf(n->parentBrowseName, sizeof(n->parentBrowseName), "%s",
                 parent->valuestring);
    if(cJSON_IsString(role))
        snprintf(n->role, sizeof(n->role), "%s", role->valuestring);

    n->simulation = cJSON_IsTrue(sim);
    n->dataType = SERVER_DT_BOOLEAN;
    if(cJSON_IsString(dtype))
        server_data_type_from_string(dtype->valuestring, &n->dataType);
    return true;
}

static cJSON *
node_to_json(const ServerNodeDef *n) {
    cJSON *obj = cJSON_CreateObject();
    cJSON_AddStringToObject(obj, "kind", n->kind);
    cJSON_AddStringToObject(obj, "browseName", n->browseName);
    cJSON_AddStringToObject(obj, "displayName", n->displayName);
    cJSON_AddStringToObject(obj, "parentBrowseName", n->parentBrowseName);
    if(strcmp(n->kind, SERVER_NODE_KIND_VARIABLE) == 0)
        cJSON_AddStringToObject(obj, "dataType", server_data_type_to_string(n->dataType));
    if(n->simulation)
        cJSON_AddTrueToObject(obj, "simulation");
    if(n->role[0])
        cJSON_AddStringToObject(obj, "role", n->role);
    return obj;
}

bool
server_config_load(ServerConfig *cfg) {
    server_config_init_path();
    server_config_seed_defaults(cfg);

    FILE *f = fopen(g_configPath, "rb");
    if(!f) {
        server_config_save(cfg);
        return true;
    }

    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if(sz <= 0) {
        fclose(f);
        server_config_save(cfg);
        return true;
    }

    char *buf = (char *)malloc((size_t)sz + 1);
    if(!buf) {
        fclose(f);
        return false;
    }
    if(fread(buf, 1, (size_t)sz, f) != (size_t)sz) {
        free(buf);
        fclose(f);
        return false;
    }
    buf[sz] = '\0';
    fclose(f);

    cJSON *root = cJSON_Parse(buf);
    free(buf);
    if(!root)
        return false;

    const cJSON *ns = cJSON_GetObjectItemCaseSensitive(root, "namespaceUri");
    if(cJSON_IsString(ns))
        snprintf(cfg->namespaceUri, sizeof(cfg->namespaceUri), "%s", ns->valuestring);

    const cJSON *nodes = cJSON_GetObjectItemCaseSensitive(root, "nodes");
    if(cJSON_IsArray(nodes)) {
        cfg->nodeCount = 0;
        const cJSON *item;
        cJSON_ArrayForEach(item, nodes) {
            if(cfg->nodeCount >= SERVER_MAX_NODES)
                break;
            if(parse_node(item, &cfg->nodes[cfg->nodeCount]))
                cfg->nodeCount++;
        }
    }

    snprintf(cfg->configPath, sizeof(cfg->configPath), "%s", g_configPath);
    cJSON_Delete(root);

    if(cfg->nodeCount == 0)
        server_config_seed_defaults(cfg);
    return true;
}

bool
server_config_save(const ServerConfig *cfg) {
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "namespaceUri", cfg->namespaceUri);

    cJSON *arr = cJSON_CreateArray();
    for(size_t i = 0; i < cfg->nodeCount; ++i)
        cJSON_AddItemToArray(arr, node_to_json(&cfg->nodes[i]));
    cJSON_AddItemToObject(root, "nodes", arr);

    char *text = cJSON_Print(root);
    cJSON_Delete(root);
    if(!text)
        return false;

    char tmp[SERVER_CONFIG_PATH_MAX + 8];
    snprintf(tmp, sizeof(tmp), "%s.tmp", g_configPath);

    FILE *f = fopen(tmp, "wb");
    if(!f) {
        free(text);
        return false;
    }
    size_t len = strlen(text);
    bool ok = fwrite(text, 1, len, f) == len;
    fclose(f);
    free(text);

    if(!ok)
        return false;
    if(rename(tmp, g_configPath) != 0)
        return false;
    return true;
}
