#ifndef DEVICE_CLIENT_H
#define DEVICE_CLIENT_H

#include "MQTTClient.h"

typedef struct DeviceClient DeviceClient;

DeviceClient* DeviceClient_create(const char* brokerUrl, const char* clientId, const char* username, const char* password);
int DeviceClient_connect(DeviceClient* client, const char* username, const char* password);
int DeviceClient_publish(DeviceClient* client, const char* topic, const char* message);
void DeviceClient_disconnect(DeviceClient* client);

#endif // DEVICE_CLIENT_H
