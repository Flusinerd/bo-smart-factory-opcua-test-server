#ifndef HTTP_SERVER_H
#define HTTP_SERVER_H

#include "node_manager.h"

#include <stdbool.h>

bool http_server_start(NodeManager *nm);
void http_server_stop(void);
void http_server_request_shutdown(void);
bool http_server_shutdown_requested(void);

#endif
