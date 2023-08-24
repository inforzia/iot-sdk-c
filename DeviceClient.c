#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "MQTTClient.h"

typedef struct {
    MQTTClient client;
    char* brokerUrl;
    char* clientId;
} DeviceClient;

DeviceClient* DeviceClient_create(const char* brokerUrl, const char* clientId, const char* username, const char* password) {
    DeviceClient* client = (DeviceClient*)malloc(sizeof(DeviceClient));
    if (!client) {
        return NULL;
    }

    client->brokerUrl = strdup(brokerUrl);
    client->clientId = strdup(clientId);

    MQTTClient_create(&(client->client), client->brokerUrl, client->clientId, MQTTCLIENT_PERSISTENCE_NONE, NULL);
    // Set username and password in connect options
    MQTTClient_connectOptions conn_opts = MQTTClient_connectOptions_initializer;
    conn_opts.username = username;
    conn_opts.password = password;

    MQTTClient_setCallbacks(client->client, NULL, NULL, NULL, NULL);
    MQTTClient_connect(client->client, &conn_opts);
    return client;
}

int DeviceClient_connect(DeviceClient* client, const char* username, const char* password) {
    int rc;
    MQTTClient_connectOptions conn_opts = MQTTClient_connectOptions_initializer;
    conn_opts.username = username;
    conn_opts.password = password;
    conn_opts.keepAliveInterval = 20;
    conn_opts.cleansession = 1;

    if ((rc = MQTTClient_connect(client->client, &conn_opts)) != MQTTCLIENT_SUCCESS) {
        printf("Failed to connect, return code %d\n", rc);
        return rc;
    } else {
    	printf("Broker URL : %s connected.\n", client->brokerUrl);
    }

    return MQTTCLIENT_SUCCESS;
}
int DeviceClient_publish(DeviceClient* client, const char* topic, const char* message) {
    MQTTClient_message pubmsg = MQTTClient_message_initializer;
    MQTTClient_deliveryToken token;
    int rc;

    pubmsg.payload = (void*)message;
    pubmsg.payloadlen = (int)strlen(message);
    pubmsg.qos = 0;
    pubmsg.retained = 0;

    MQTTClient_publishMessage(client->client, topic, &pubmsg, &token);
    rc = MQTTClient_waitForCompletion(client->client, token, 10000L);

    return rc;
}

void DeviceClient_disconnect(DeviceClient* client) {
    MQTTClient_disconnect(client->client, 10000);
    MQTTClient_destroy(&(client->client));
    free(client->brokerUrl);
    free(client->clientId);
    free(client);
}
