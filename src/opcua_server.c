#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include <open62541/server.h>
#include <open62541/server_config_default.h>

#define SENSOR_COUNT    5
#define SIM_INTERVAL_MS 1000.0

typedef struct {
    UA_NodeId nodeId;
    UA_Boolean value;
    UA_Boolean overridden;
    UA_Boolean exemptFromSimulation;
} SensorState;

typedef struct {
    SensorState sensors[SENSOR_COUNT];
    UA_Boolean simulationEnabled;
} SensorContext;

static SensorContext g_sensorContext;

static void
sensorWriteCallback(UA_Server *server,
                    const UA_NodeId *sessionId,
                    void *sessionContext,
                    const UA_NodeId *nodeId,
                    void *nodeContext,
                    const UA_NumericRange *range,
                    const UA_DataValue *data) {
    (void)server;
    (void)sessionId;
    (void)sessionContext;
    (void)nodeId;
    (void)range;

    SensorState *state = (SensorState *)nodeContext;
    if(!state) {
        return;
    }

    if(!data || !data->hasValue || !data->value.data ||
       data->value.type != &UA_TYPES[UA_TYPES_BOOLEAN]) {
        return;
    }

    UA_Boolean *incoming = (UA_Boolean *)data->value.data;
    state->value = *incoming;
    state->overridden = UA_TRUE;
}

static void
simulationEnabledWriteCallback(UA_Server *server,
                                const UA_NodeId *sessionId,
                                void *sessionContext,
                                const UA_NodeId *nodeId,
                                void *nodeContext,
                                const UA_NumericRange *range,
                                const UA_DataValue *data) {
    (void)server;
    (void)sessionId;
    (void)sessionContext;
    (void)nodeId;
    (void)range;

    SensorContext *ctx = (SensorContext *)nodeContext;
    if(!ctx) {
        return;
    }

    if(!data || !data->hasValue || !data->value.data ||
       data->value.type != &UA_TYPES[UA_TYPES_BOOLEAN]) {
        return;
    }

    UA_Boolean *incoming = (UA_Boolean *)data->value.data;
    ctx->simulationEnabled = *incoming;

    if(!*incoming) {
        for(size_t i = 0; i < SENSOR_COUNT; ++i) {
            SensorState *state = &ctx->sensors[i];
            if(state->exemptFromSimulation) {
                continue;
            }
            state->value = UA_FALSE;
            state->overridden = UA_FALSE;

            UA_Variant value;
            UA_Variant_init(&value);
            UA_Variant_setScalar(&value, &state->value,
                                 &UA_TYPES[UA_TYPES_BOOLEAN]);
            UA_Server_writeValue(server, state->nodeId, value);
        }
    } else {
        for(size_t i = 0; i < SENSOR_COUNT; ++i) {
            if(ctx->sensors[i].exemptFromSimulation) {
                continue;
            }
            ctx->sensors[i].overridden = UA_FALSE;
        }
    }
}

static UA_StatusCode
addSensorsFolder(UA_Server *server, UA_UInt16 nsIdx, UA_NodeId *outFolderId) {
    UA_ObjectAttributes attr = UA_ObjectAttributes_default;
    attr.displayName = UA_LOCALIZEDTEXT("en-US", "Sensors");

    UA_NodeId folderId;
    UA_StatusCode retval = UA_Server_addObjectNode(
        server,
        UA_NODEID_STRING(nsIdx, "Sensors"),
        UA_NODEID_NUMERIC(0, UA_NS0ID_OBJECTSFOLDER),
        UA_NODEID_NUMERIC(0, UA_NS0ID_ORGANIZES),
        UA_QUALIFIEDNAME(nsIdx, "Sensors"),
        UA_NODEID_NUMERIC(0, UA_NS0ID_FOLDERTYPE),
        attr,
        NULL,
        &folderId);

    if(retval == UA_STATUSCODE_GOOD && outFolderId) {
        *outFolderId = folderId;
    }

    return retval;
}

static UA_StatusCode
addSensorVariable(UA_Server *server,
                  SensorContext *ctx,
                  size_t index,
                  UA_UInt16 nsIdx,
                  const UA_NodeId *parentFolderId,
                  const char *browseName,
                  const char *displayName,
                  UA_Boolean exemptFromSimulation) {
    char nameBuf[64];
    if(browseName) {
        snprintf(nameBuf, sizeof(nameBuf), "%s", browseName);
    } else {
        snprintf(nameBuf, sizeof(nameBuf), "Sensor%zu", index + 1);
    }

    char displayBuf[64];
    if(displayName) {
        snprintf(displayBuf, sizeof(displayBuf), "%s", displayName);
    } else {
        snprintf(displayBuf, sizeof(displayBuf), "%s", nameBuf);
    }

    UA_VariableAttributes vAttr = UA_VariableAttributes_default;
    UA_Boolean initial = UA_FALSE;
    UA_Variant_setScalar(&vAttr.value, &initial, &UA_TYPES[UA_TYPES_BOOLEAN]);
    vAttr.displayName = UA_LOCALIZEDTEXT("en-US", displayBuf);
    vAttr.description = UA_LOCALIZEDTEXT("en-US", "Binary sensor");
    vAttr.dataType = UA_TYPES[UA_TYPES_BOOLEAN].typeId;
    vAttr.accessLevel =
        UA_ACCESSLEVELMASK_READ | UA_ACCESSLEVELMASK_WRITE;
    vAttr.userAccessLevel =
        UA_ACCESSLEVELMASK_READ | UA_ACCESSLEVELMASK_WRITE;

    UA_NodeId nodeId;
    UA_StatusCode retval = UA_Server_addVariableNode(
        server,
        UA_NODEID_STRING(nsIdx, nameBuf),
        *parentFolderId,
        UA_NODEID_NUMERIC(0, UA_NS0ID_ORGANIZES),
        UA_QUALIFIEDNAME(nsIdx, nameBuf),
        UA_NODEID_NUMERIC(0, UA_NS0ID_BASEDATAVARIABLETYPE),
        vAttr,
        NULL,
        &nodeId);

    if(retval != UA_STATUSCODE_GOOD) {
        return retval;
    }

    SensorState *state = &ctx->sensors[index];
    state->nodeId = nodeId;
    state->value = initial;
    state->overridden = UA_FALSE;
    state->exemptFromSimulation = exemptFromSimulation;

    UA_Server_setNodeContext(server, nodeId, state);

    UA_ValueCallback cb;
    cb.onRead = NULL;
    cb.onWrite = sensorWriteCallback;
    UA_Server_setVariableNode_valueCallback(server, nodeId, cb);

    return UA_STATUSCODE_GOOD;
}

static UA_StatusCode
addSimulationEnabledVariable(UA_Server *server,
                              SensorContext *ctx,
                              UA_UInt16 nsIdx,
                              const UA_NodeId *parentFolderId) {
    UA_VariableAttributes vAttr = UA_VariableAttributes_default;
    UA_Boolean initial = UA_TRUE;
    UA_Variant_setScalar(&vAttr.value, &initial, &UA_TYPES[UA_TYPES_BOOLEAN]);
    vAttr.displayName = UA_LOCALIZEDTEXT("en-US", "SimulationEnabled");
    vAttr.description =
        UA_LOCALIZEDTEXT("en-US", "Enable/disable automatic sensor toggling");
    vAttr.dataType = UA_TYPES[UA_TYPES_BOOLEAN].typeId;
    vAttr.accessLevel =
        UA_ACCESSLEVELMASK_READ | UA_ACCESSLEVELMASK_WRITE;
    vAttr.userAccessLevel =
        UA_ACCESSLEVELMASK_READ | UA_ACCESSLEVELMASK_WRITE;

    UA_NodeId nodeId;
    UA_StatusCode retval = UA_Server_addVariableNode(
        server,
        UA_NODEID_STRING(nsIdx, "SimulationEnabled"),
        *parentFolderId,
        UA_NODEID_NUMERIC(0, UA_NS0ID_ORGANIZES),
        UA_QUALIFIEDNAME(nsIdx, "SimulationEnabled"),
        UA_NODEID_NUMERIC(0, UA_NS0ID_BASEDATAVARIABLETYPE),
        vAttr,
        NULL,
        &nodeId);

    if(retval != UA_STATUSCODE_GOOD) {
        return retval;
    }

    UA_Server_setNodeContext(server, nodeId, ctx);

    UA_ValueCallback cb;
    cb.onRead = NULL;
    cb.onWrite = simulationEnabledWriteCallback;
    UA_Server_setVariableNode_valueCallback(server, nodeId, cb);

    return UA_STATUSCODE_GOOD;
}

static void
simulationCallback(UA_Server *server, void *data) {
    SensorContext *ctx = (SensorContext *)data;
    if(!ctx || !ctx->simulationEnabled) {
        return;
    }

    for(size_t i = 0; i < SENSOR_COUNT; ++i) {
        SensorState *state = &ctx->sensors[i];
        if(state->exemptFromSimulation || state->overridden) {
            continue;
        }

        UA_Variant currentValue;
        UA_Variant_init(&currentValue);
        UA_StatusCode retval = UA_Server_readValue(server, state->nodeId, &currentValue);
        if(retval != UA_STATUSCODE_GOOD || 
           !currentValue.data || 
           currentValue.type != &UA_TYPES[UA_TYPES_BOOLEAN]) {
            UA_Variant_clear(&currentValue);
            continue;
        }

        UA_Boolean current = *(UA_Boolean *)currentValue.data;
        UA_Boolean newValue = (current == UA_FALSE) ? UA_TRUE : UA_FALSE;
        UA_Variant_clear(&currentValue);

        state->value = newValue;

        UA_Variant value;
        UA_Variant_init(&value);
        UA_Variant_setScalar(&value, &newValue,
                             &UA_TYPES[UA_TYPES_BOOLEAN]);
        UA_Server_writeValue(server, state->nodeId, value);
        
        state->overridden = UA_FALSE;
    }
}

int main(void) {
    UA_Server *server = UA_Server_new();
    if(!server) {
        return EXIT_FAILURE;
    }

    UA_ServerConfig *config = UA_Server_getConfig(server);
    UA_ServerConfig_setDefault(config);

    config->applicationDescription.applicationName =
        UA_LOCALIZEDTEXT_ALLOC("en-US", "BinarySensorsDemoServer");

    UA_UInt16 nsIdx =
        UA_Server_addNamespace(server, "urn:binary-sensors-demo");

    g_sensorContext.simulationEnabled = UA_TRUE;

    UA_NodeId sensorsFolderId;
    UA_StatusCode retval = addSensorsFolder(server, nsIdx, &sensorsFolderId);
    if(retval != UA_STATUSCODE_GOOD) {
        UA_Server_delete(server);
        return EXIT_FAILURE;
    }

    for(size_t i = 0; i < SENSOR_COUNT - 1; ++i) {
        retval = addSensorVariable(
            server, &g_sensorContext, i, nsIdx, &sensorsFolderId,
            NULL, NULL, UA_FALSE);
        if(retval != UA_STATUSCODE_GOOD) {
            UA_Server_delete(server);
            return EXIT_FAILURE;
        }
    }

    retval = addSensorVariable(
        server, &g_sensorContext, SENSOR_COUNT - 1, nsIdx, &sensorsFolderId,
        "Pi1_InductionSwitch1", "Induction Switch 1 (Pi 1)", UA_TRUE);
    if(retval != UA_STATUSCODE_GOOD) {
        UA_Server_delete(server);
        return EXIT_FAILURE;
    }

    retval = addSimulationEnabledVariable(
        server, &g_sensorContext, nsIdx, &sensorsFolderId);
    if(retval != UA_STATUSCODE_GOOD) {
        UA_Server_delete(server);
        return EXIT_FAILURE;
    }

    UA_UInt64 callbackId = 0;
    retval = UA_Server_addRepeatedCallback(
        server, simulationCallback, &g_sensorContext, SIM_INTERVAL_MS,
        &callbackId);
    if(retval != UA_STATUSCODE_GOOD) {
        UA_Server_delete(server);
        return EXIT_FAILURE;
    }

    UA_StatusCode runStatus = UA_Server_run(server, &(UA_Boolean){true});

    UA_Server_delete(server);
    return (runStatus == UA_STATUSCODE_GOOD) ? EXIT_SUCCESS : EXIT_FAILURE;
}

