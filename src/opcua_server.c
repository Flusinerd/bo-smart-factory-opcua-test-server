#include <signal.h>
#include <stdio.h>
#include <stdlib.h>

#include <open62541/server.h>
#include <open62541/server_config_default.h>

#include "http_server.h"
#include "node_manager.h"
#include "server_config.h"

static UA_Server *g_server = NULL;
static UA_Boolean g_running = UA_TRUE;
static NodeManager *g_nm = NULL;

static void
signal_handler(int sig) {
    (void)sig;
    g_running = UA_FALSE;
    http_server_request_shutdown();
}

int
main(void) {
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    server_config_init_path();
    ServerConfig cfg;
    if(!server_config_load(&cfg)) {
        fprintf(stderr, "Failed to load server config\n");
        return EXIT_FAILURE;
    }

    const char *nsUri = server_config_resolve_namespace_uri(&cfg);
    printf("Namespace URI: %s\n", nsUri);
    printf("Config path: %s\n", server_config_get_path());

    g_server = UA_Server_new();
    if(!g_server)
        return EXIT_FAILURE;

    UA_ServerConfig *config = UA_Server_getConfig(g_server);
    UA_ServerConfig_setDefault(config);
    config->applicationDescription.applicationName =
        UA_LOCALIZEDTEXT_ALLOC("en-US", "BinarySensorsDemoServer");

    UA_UInt16 nsIdx = UA_Server_addNamespace(g_server, nsUri);
    printf("Namespace index: %u\n", (unsigned)nsIdx);

    g_nm = node_manager_create(g_server, nsIdx, &cfg);
    if(!g_nm) {
        UA_Server_delete(g_server);
        return EXIT_FAILURE;
    }

    UA_StatusCode retval = node_manager_build(g_nm);
    if(retval != UA_STATUSCODE_GOOD) {
        fprintf(stderr, "Failed to build address space: %s\n",
                UA_StatusCode_name(retval));
        node_manager_destroy(g_nm);
        UA_Server_delete(g_server);
        return EXIT_FAILURE;
    }

    UA_UInt64 callbackId = 0;
    retval = UA_Server_addRepeatedCallback(
        g_server, node_manager_simulation_callback, g_nm, 1000.0, &callbackId);
    if(retval != UA_STATUSCODE_GOOD) {
        fprintf(stderr, "Failed to add simulation callback\n");
        node_manager_destroy(g_nm);
        UA_Server_delete(g_server);
        return EXIT_FAILURE;
    }

    if(!http_server_start(g_nm)) {
        fprintf(stderr, "HTTP server failed to start\n");
        node_manager_destroy(g_nm);
        UA_Server_delete(g_server);
        return EXIT_FAILURE;
    }

    while(g_running) {
        if(http_server_shutdown_requested())
            g_running = UA_FALSE;
        UA_Server_run_iterate(g_server, true);
    }

    http_server_stop();
    node_manager_destroy(g_nm);
    UA_Server_delete(g_server);
    return EXIT_SUCCESS;
}
