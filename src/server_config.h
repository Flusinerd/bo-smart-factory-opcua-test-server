#ifndef SERVER_CONFIG_H
#define SERVER_CONFIG_H

#include <stdbool.h>
#include <stddef.h>

#define SERVER_NODE_KIND_FOLDER    "folder"
#define SERVER_NODE_KIND_VARIABLE  "variable"
#define SERVER_ROLE_SIMULATION_ENABLED "simulationEnabled"

#define SERVER_MAX_NODES        256
#define SERVER_BROWSE_NAME_MAX  64
#define SERVER_DISPLAY_NAME_MAX 128
#define SERVER_NAMESPACE_MAX    256
#define SERVER_CONFIG_PATH_MAX  512

typedef enum {
    SERVER_DT_BOOLEAN,
    SERVER_DT_INT32,
    SERVER_DT_DOUBLE,
    SERVER_DT_STRING
} ServerDataType;

typedef struct {
    char kind[16];
    char browseName[SERVER_BROWSE_NAME_MAX];
    char displayName[SERVER_DISPLAY_NAME_MAX];
    char parentBrowseName[SERVER_BROWSE_NAME_MAX];
    ServerDataType dataType;
    bool simulation;
    char role[32];
} ServerNodeDef;

typedef struct {
    char namespaceUri[SERVER_NAMESPACE_MAX];
    ServerNodeDef nodes[SERVER_MAX_NODES];
    size_t nodeCount;
    char configPath[SERVER_CONFIG_PATH_MAX];
} ServerConfig;

void server_config_init_path(void);
const char *server_config_get_path(void);
void server_config_seed_defaults(ServerConfig *cfg);
bool server_config_load(ServerConfig *cfg);
bool server_config_save(const ServerConfig *cfg);
const char *server_config_resolve_namespace_uri(const ServerConfig *cfg);
#define SERVER_BROWSE_NAME_INVALID_MSG \
    "browseName may only contain letters, digits, and underscores (no spaces)"

bool server_config_validate_browse_name(const char *name);
const char *server_data_type_to_string(ServerDataType dt);
bool server_data_type_from_string(const char *s, ServerDataType *out);

#endif
