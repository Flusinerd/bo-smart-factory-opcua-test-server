#include "http_server.h"

#include <cJSON.h>
#include <microhttpd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static NodeManager *g_nm = NULL;
static struct MHD_Daemon *g_daemon = NULL;
static volatile int g_shutdown_requested = 0;
static char g_webRoot[512] = "./web/opcua-server";

#define POST_RECV_MARKER ((void *)0x1)

static void
init_web_root(void) {
    const char *env = getenv("OPCUA_HTTP_WEB_ROOT");
    if(env && env[0])
        snprintf(g_webRoot, sizeof(g_webRoot), "%s", env);
}

static enum MHD_Result
send_json(struct MHD_Connection *conn, unsigned int status, const char *json) {
    struct MHD_Response *resp =
        MHD_create_response_from_buffer(strlen(json), (void *)json,
                                        MHD_RESPMEM_MUST_COPY);
    if(!resp)
        return MHD_NO;
    MHD_add_response_header(resp, "Content-Type", "application/json");
    MHD_add_response_header(resp, "Access-Control-Allow-Origin", "*");
    enum MHD_Result ret = MHD_queue_response(conn, status, resp);
    MHD_destroy_response(resp);
    return ret;
}

static enum MHD_Result
send_error_json(struct MHD_Connection *conn, unsigned int status, const char *msg) {
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "error", msg);
    char *text = cJSON_PrintUnformatted(o);
    cJSON_Delete(o);
    if(!text)
        return MHD_NO;
    int r = send_json(conn, status, text);
    free(text);
    return r;
}

static char *
read_post_body(struct MHD_Connection *conn, const char *upload_data,
               size_t *upload_data_size, void **con_cls) {
    (void)conn;
    if(*con_cls == NULL) {
        if(*upload_data_size > 0) {
            char *buf = malloc(*upload_data_size + 1);
            if(!buf)
                return NULL;
            memcpy(buf, upload_data, *upload_data_size);
            buf[*upload_data_size] = '\0';
            *con_cls = buf;
            *upload_data_size = 0;
            return buf;
        }
        *con_cls = strdup("");
        return (char *)(*con_cls);
    }
    if(*upload_data_size == 0)
        return (char *)(*con_cls);

    size_t old = strlen((char *)(*con_cls));
    char *buf = realloc(*con_cls, old + *upload_data_size + 1);
    if(!buf)
        return NULL;
    memcpy(buf + old, upload_data, *upload_data_size);
    buf[old + *upload_data_size] = '\0';
    *con_cls = buf;
    *upload_data_size = 0;
    return buf;
}

static void
free_con_cls(void **con_cls) {
    if(con_cls && *con_cls) {
        free(*con_cls);
        *con_cls = NULL;
    }
}

static char *
node_id_string(UA_UInt16 ns, const char *browseName, char *buf, size_t bufsz) {
    snprintf(buf, bufsz, "ns=%u;s=%s", (unsigned)ns, browseName);
    return buf;
}

static enum MHD_Result
handle_get_config(struct MHD_Connection *conn) {
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "namespaceUri", g_nm->config.namespaceUri);
    cJSON_AddStringToObject(o, "configPath", server_config_get_path());
    char *text = cJSON_PrintUnformatted(o);
    cJSON_Delete(o);
    if(!text)
        return MHD_NO;
    int r = send_json(conn, MHD_HTTP_OK, text);
    free(text);
    return r;
}

static enum MHD_Result
handle_put_config(struct MHD_Connection *conn, const char *body) {
    cJSON *root = cJSON_Parse(body);
    if(!root)
        return send_error_json(conn, MHD_HTTP_BAD_REQUEST, "invalid json");
    const cJSON *ns = cJSON_GetObjectItemCaseSensitive(root, "namespaceUri");
    if(!cJSON_IsString(ns)) {
        cJSON_Delete(root);
        return send_error_json(conn, MHD_HTTP_BAD_REQUEST, "namespaceUri required");
    }
    pthread_mutex_lock(&g_nm->mutex);
    snprintf(g_nm->config.namespaceUri, sizeof(g_nm->config.namespaceUri), "%s",
             ns->valuestring);
    node_manager_save_config(g_nm);
    pthread_mutex_unlock(&g_nm->mutex);
    cJSON_Delete(root);

    cJSON *resp = cJSON_CreateObject();
    cJSON_AddBoolToObject(resp, "ok", true);
    cJSON_AddStringToObject(resp, "message", "Restart server to apply namespace change");
    char *text = cJSON_PrintUnformatted(resp);
    cJSON_Delete(resp);
    int r = send_json(conn, MHD_HTTP_OK, text);
    free(text);
    return r;
}

static enum MHD_Result
handle_get_nodes(struct MHD_Connection *conn) {
    cJSON *arr = cJSON_CreateArray();
    pthread_mutex_lock(&g_nm->mutex);
    for(size_t i = 0; i < g_nm->config.nodeCount; ++i) {
        const ServerNodeDef *n = &g_nm->config.nodes[i];
        cJSON *o = cJSON_CreateObject();
        cJSON_AddStringToObject(o, "kind", n->kind);
        cJSON_AddStringToObject(o, "browseName", n->browseName);
        cJSON_AddStringToObject(o, "displayName", n->displayName);
        cJSON_AddStringToObject(o, "parentBrowseName", n->parentBrowseName);
        if(strcmp(n->kind, SERVER_NODE_KIND_VARIABLE) == 0)
            cJSON_AddStringToObject(o, "dataType", server_data_type_to_string(n->dataType));
        if(n->simulation)
            cJSON_AddTrueToObject(o, "simulation");
        if(n->role[0])
            cJSON_AddStringToObject(o, "role", n->role);
        char nid[128];
        cJSON_AddStringToObject(o, "nodeIdString",
                                node_id_string(g_nm->nsIdx, n->browseName, nid, sizeof(nid)));
        cJSON_AddItemToArray(arr, o);
    }
    pthread_mutex_unlock(&g_nm->mutex);
    char *text = cJSON_PrintUnformatted(arr);
    cJSON_Delete(arr);
    if(!text)
        return MHD_NO;
    int r = send_json(conn, MHD_HTTP_OK, text);
    free(text);
    return r;
}

static enum MHD_Result
handle_get_status(struct MHD_Connection *conn) {
    cJSON *o = cJSON_CreateObject();
    pthread_mutex_lock(&g_nm->mutex);
    cJSON_AddStringToObject(o, "namespaceUri", g_nm->config.namespaceUri);
    cJSON_AddNumberToObject(o, "namespaceIndex", g_nm->nsIdx);
    cJSON_AddBoolToObject(o, "simulationEnabled", g_nm->simulationEnabled);
    cJSON *sensors = cJSON_CreateArray();
    for(size_t i = 0; i < g_nm->sensorCount; ++i) {
        const SimulatedSensor *st = &g_nm->sensors[i];
        cJSON *s = cJSON_CreateObject();
        cJSON_AddStringToObject(s, "browseName", st->browseName);
        cJSON_AddBoolToObject(s, "value", st->value);
        cJSON_AddBoolToObject(s, "overridden", st->overridden);
        cJSON_AddItemToArray(sensors, s);
    }
    cJSON_AddItemToObject(o, "sensors", sensors);
    pthread_mutex_unlock(&g_nm->mutex);
    char *text = cJSON_PrintUnformatted(o);
    cJSON_Delete(o);
    if(!text)
        return MHD_NO;
    int r = send_json(conn, MHD_HTTP_OK, text);
    free(text);
    return r;
}

static enum MHD_Result
handle_put_simulation(struct MHD_Connection *conn, const char *body) {
    cJSON *root = cJSON_Parse(body);
    if(!root)
        return send_error_json(conn, MHD_HTTP_BAD_REQUEST, "invalid json");
    const cJSON *en = cJSON_GetObjectItemCaseSensitive(root, "enabled");
    if(!cJSON_IsBool(en)) {
        cJSON_Delete(root);
        return send_error_json(conn, MHD_HTTP_BAD_REQUEST, "enabled required");
    }
    node_manager_set_simulation_enabled(g_nm, cJSON_IsTrue(en) ? UA_TRUE : UA_FALSE);
    cJSON_Delete(root);
    cJSON *resp = cJSON_CreateObject();
    cJSON_AddBoolToObject(resp, "ok", true);
    char *text = cJSON_PrintUnformatted(resp);
    cJSON_Delete(resp);
    int r = send_json(conn, MHD_HTTP_OK, text);
    free(text);
    return r;
}

static const char *
add_node_status_message(UA_StatusCode rc) {
    if(rc == UA_STATUSCODE_BADINVALIDARGUMENT)
        return SERVER_BROWSE_NAME_INVALID_MSG;
    if(rc == UA_STATUSCODE_BADNODEIDEXISTS)
        return "browseName already exists";
    return UA_StatusCode_name(rc);
}

static enum MHD_Result
handle_post_folder(struct MHD_Connection *conn, const char *body) {
    cJSON *root = cJSON_Parse(body);
    if(!root)
        return send_error_json(conn, MHD_HTTP_BAD_REQUEST, "invalid json");
    const cJSON *parent = cJSON_GetObjectItemCaseSensitive(root, "parentBrowseName");
    const cJSON *browse = cJSON_GetObjectItemCaseSensitive(root, "browseName");
    const cJSON *display = cJSON_GetObjectItemCaseSensitive(root, "displayName");
    if(!cJSON_IsString(browse) || !cJSON_IsString(display)) {
        cJSON_Delete(root);
        return send_error_json(conn, MHD_HTTP_BAD_REQUEST, "browseName and displayName required");
    }
    if(!server_config_validate_browse_name(browse->valuestring)) {
        cJSON_Delete(root);
        return send_error_json(conn, MHD_HTTP_BAD_REQUEST, SERVER_BROWSE_NAME_INVALID_MSG);
    }
    const char *parentName = cJSON_IsString(parent) ? parent->valuestring : "";
    UA_StatusCode rc = node_manager_add_folder(g_nm, parentName, browse->valuestring,
                                               display->valuestring);
    cJSON_Delete(root);
    if(rc != UA_STATUSCODE_GOOD)
        return send_error_json(conn, MHD_HTTP_BAD_REQUEST, add_node_status_message(rc));
    cJSON *resp = cJSON_CreateObject();
    cJSON_AddBoolToObject(resp, "ok", true);
    char *text = cJSON_PrintUnformatted(resp);
    cJSON_Delete(resp);
    int r = send_json(conn, MHD_HTTP_OK, text);
    free(text);
    return r;
}

static enum MHD_Result
handle_post_variable(struct MHD_Connection *conn, const char *body) {
    cJSON *root = cJSON_Parse(body);
    if(!root)
        return send_error_json(conn, MHD_HTTP_BAD_REQUEST, "invalid json");
    const cJSON *parent = cJSON_GetObjectItemCaseSensitive(root, "parentBrowseName");
    const cJSON *browse = cJSON_GetObjectItemCaseSensitive(root, "browseName");
    const cJSON *display = cJSON_GetObjectItemCaseSensitive(root, "displayName");
    const cJSON *dtype = cJSON_GetObjectItemCaseSensitive(root, "dataType");
    const cJSON *sim = cJSON_GetObjectItemCaseSensitive(root, "simulation");
    if(!cJSON_IsString(browse) || !cJSON_IsString(display)) {
        cJSON_Delete(root);
        return send_error_json(conn, MHD_HTTP_BAD_REQUEST, "browseName and displayName required");
    }
    if(!server_config_validate_browse_name(browse->valuestring)) {
        cJSON_Delete(root);
        return send_error_json(conn, MHD_HTTP_BAD_REQUEST, SERVER_BROWSE_NAME_INVALID_MSG);
    }
    ServerDataType dt = SERVER_DT_BOOLEAN;
    if(cJSON_IsString(dtype) && !server_data_type_from_string(dtype->valuestring, &dt)) {
        cJSON_Delete(root);
        return send_error_json(conn, MHD_HTTP_BAD_REQUEST,
                               "dataType must be Boolean, Int32, Double, or String");
    }
    if(cJSON_IsTrue(sim) && dt != SERVER_DT_BOOLEAN) {
        cJSON_Delete(root);
        return send_error_json(conn, MHD_HTTP_BAD_REQUEST,
                               "simulation is only supported for Boolean variables");
    }
    const char *parentName = cJSON_IsString(parent) ? parent->valuestring : "";
    UA_StatusCode rc = node_manager_add_variable(
        g_nm, parentName, browse->valuestring, display->valuestring, dt,
        cJSON_IsTrue(sim));
    cJSON_Delete(root);
    if(rc != UA_STATUSCODE_GOOD)
        return send_error_json(conn, MHD_HTTP_BAD_REQUEST, add_node_status_message(rc));
    cJSON *resp = cJSON_CreateObject();
    cJSON_AddBoolToObject(resp, "ok", true);
    char *text = cJSON_PrintUnformatted(resp);
    cJSON_Delete(resp);
    int r = send_json(conn, MHD_HTTP_OK, text);
    free(text);
    return r;
}

static const char *
url_after_prefix(const char *url, const char *prefix) {
    size_t n = strlen(prefix);
    if(strncmp(url, prefix, n) != 0)
        return NULL;
    return url + n;
}

static enum MHD_Result
serve_static(struct MHD_Connection *conn, const char *url) {
    char path[768];
    const char *rel = url;
    if(strcmp(rel, "/") == 0)
        rel = "/index.html";
    snprintf(path, sizeof(path), "%s%s", g_webRoot, rel);

    FILE *f = fopen(path, "rb");
    if(!f)
        return MHD_NO;

    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if(sz < 0) {
        fclose(f);
        return MHD_NO;
    }

    char *buf = malloc((size_t)sz);
    if(!buf) {
        fclose(f);
        return MHD_NO;
    }
    if(fread(buf, 1, (size_t)sz, f) != (size_t)sz) {
        free(buf);
        fclose(f);
        return MHD_NO;
    }
    fclose(f);

    const char *ctype = "text/plain";
    if(strstr(rel, ".html"))
        ctype = "text/html; charset=utf-8";
    else if(strstr(rel, ".css"))
        ctype = "text/css";
    else if(strstr(rel, ".js"))
        ctype = "application/javascript";

    struct MHD_Response *resp =
        MHD_create_response_from_buffer((size_t)sz, buf, MHD_RESPMEM_MUST_FREE);
    if(!resp) {
        free(buf);
        return MHD_NO;
    }
    MHD_add_response_header(resp, "Content-Type", ctype);
    enum MHD_Result ret = MHD_queue_response(conn, MHD_HTTP_OK, resp);
    MHD_destroy_response(resp);
    return ret;
}

static enum MHD_Result
access_handler(void *cls, struct MHD_Connection *conn, const char *url,
               const char *method, const char *version, const char *upload_data,
               size_t *upload_data_size, void **con_cls) {
    (void)cls;
    (void)version;

    if(strcmp(method, "OPTIONS") == 0) {
        struct MHD_Response *resp = MHD_create_response_from_buffer(0, "", MHD_RESPMEM_PERSISTENT);
        MHD_add_response_header(resp, "Access-Control-Allow-Origin", "*");
        MHD_add_response_header(resp, "Access-Control-Allow-Methods", "GET, PUT, POST, DELETE, OPTIONS");
        MHD_add_response_header(resp, "Access-Control-Allow-Headers", "Content-Type");
        enum MHD_Result ret = MHD_queue_response(conn, MHD_HTTP_NO_CONTENT, resp);
        MHD_destroy_response(resp);
        return ret;
    }

    const char *body = "";
    if(strcmp(method, "PUT") == 0 || strcmp(method, "POST") == 0) {
        if(*upload_data_size > 0) {
            if(*con_cls == POST_RECV_MARKER)
                *con_cls = NULL;
            char *b = read_post_body(conn, upload_data, upload_data_size, con_cls);
            if(!b)
                return MHD_YES;
            return MHD_YES;
        }
        if(*con_cls == NULL) {
            *con_cls = POST_RECV_MARKER;
            return MHD_YES;
        }
        if(*con_cls == POST_RECV_MARKER)
            *con_cls = NULL;
        char *b = read_post_body(conn, upload_data, upload_data_size, con_cls);
        if(!b)
            return MHD_YES;
        body = b;
    }

    enum MHD_Result apiResult = MHD_NO;

    if(strcmp(method, "GET") == 0 && strcmp(url, "/api/config") == 0)
        apiResult = handle_get_config(conn);
    else if(strcmp(method, "PUT") == 0 && strcmp(url, "/api/config") == 0)
        apiResult = handle_put_config(conn, body);
    else if(strcmp(method, "GET") == 0 && strcmp(url, "/api/nodes") == 0)
        apiResult = handle_get_nodes(conn);
    else if(strcmp(method, "GET") == 0 && strcmp(url, "/api/status") == 0)
        apiResult = handle_get_status(conn);
    else if(strcmp(method, "PUT") == 0 && strcmp(url, "/api/simulation") == 0)
        apiResult = handle_put_simulation(conn, body);
    else if(strcmp(method, "POST") == 0 && strcmp(url, "/api/reset-overrides") == 0) {
        node_manager_reset_overrides(g_nm);
        apiResult = send_json(conn, MHD_HTTP_OK, "{\"ok\":true}");
    } else if(strcmp(method, "POST") == 0 && strcmp(url, "/api/restart") == 0) {
        http_server_request_shutdown();
        apiResult = send_json(conn, MHD_HTTP_OK, "{\"ok\":true}");
    } else if(strcmp(method, "POST") == 0 && strcmp(url, "/api/nodes/folder") == 0)
        apiResult = handle_post_folder(conn, body);
    else if(strcmp(method, "POST") == 0 && strcmp(url, "/api/nodes/variable") == 0)
        apiResult = handle_post_variable(conn, body);
    else if(strcmp(method, "DELETE") == 0) {
        const char *name = url_after_prefix(url, "/api/nodes/");
        if(name && name[0]) {
            UA_StatusCode rc = node_manager_delete(g_nm, name);
            if(rc != UA_STATUSCODE_GOOD)
                apiResult = send_error_json(conn, MHD_HTTP_BAD_REQUEST,
                                            UA_StatusCode_name(rc));
            else
                apiResult = send_json(conn, MHD_HTTP_OK, "{\"ok\":true}");
        }
    }

    if(apiResult != MHD_NO) {
        free_con_cls(con_cls);
        return apiResult;
    }

    if(strcmp(method, "GET") == 0 && strncmp(url, "/api/", 5) != 0) {
        enum MHD_Result r = serve_static(conn, url);
        if(r != MHD_NO)
            return r;
    }

    free_con_cls(con_cls);
    return send_error_json(conn, MHD_HTTP_NOT_FOUND, "not found");
}

bool
http_server_start(NodeManager *nm) {
    g_nm = nm;
    init_web_root();
    const char *addr = getenv("OPCUA_HTTP_ADDR");
    if(!addr || !addr[0])
        addr = ":8081";

    unsigned int port = 8081;
    if(addr[0] == ':')
        port = (unsigned int)atoi(addr + 1);

    g_daemon = MHD_start_daemon(MHD_USE_SELECT_INTERNALLY, port, NULL, NULL,
                                &access_handler, NULL, MHD_OPTION_END);
    if(!g_daemon) {
        fprintf(stderr, "http: failed to start on port %u\n", port);
        return false;
    }
    printf("HTTP config UI listening on %s\n", addr);
    return true;
}

void
http_server_stop(void) {
    if(g_daemon) {
        MHD_stop_daemon(g_daemon);
        g_daemon = NULL;
    }
}

void
http_server_request_shutdown(void) {
    g_shutdown_requested = 1;
}

bool
http_server_shutdown_requested(void) {
    return g_shutdown_requested != 0;
}
