#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include "DeviceClient.h"

#define SUCCESS 0

float randomFloat(float min, float max) {
    float scale = rand() / (float)RAND_MAX;
    return min + scale * (max - min);
}

int main(int argc, char* argv[]) {
    if (argc != 4) {
        printf("Usage: %s <address> <username> <password>\n", argv[0]);
        return EXIT_FAILURE;
    }

    const char* address = argv[1];
    const char* username = argv[2];
    const char* password = argv[3];

    const char clientId[] = "iot_sdk_c";

    DeviceClient* client = DeviceClient_create(address, clientId, username, password);
    if (!client) {
        printf("Failed to create DeviceClient\n");
        return EXIT_FAILURE;
    }

    if (DeviceClient_connect(client, username, password) != SUCCESS) {
        printf("Failed to connect to MQTT broker\n");
        DeviceClient_disconnect(client);
        return EXIT_FAILURE;
    }

    float temperature = randomFloat(30.0, 33.0);
    float soil_moisture = randomFloat(54.0, 55.0);

    const char* topic = "flomon/telemetry";

    char message[256];
    snprintf(message, sizeof(message), "{\"deviceKey\":\"env-B14\",\"temperature\":%.2f,\"soil_moisture\":%.2f}", temperature, soil_moisture);

    int publish_result = DeviceClient_publish(client, topic, message);
    if (publish_result == SUCCESS) {
        printf("Message published to topic: %s\n", topic);
        printf("Message : %s\n", message);
    } else if (publish_result == -3) {
       // Retry publish after reconnect
        printf("Publish failed. Retrying after reconnect...\n");
        DeviceClient_disconnect(client);

        // Reconnect
        if (DeviceClient_connect(client, username, password) != SUCCESS) {
		printf("Reconnect failed. Exiting...\n");
            	DeviceClient_disconnect(client);
            	return EXIT_FAILURE;
        }
    } else {
    	printf("Publish failed.\n");	    
    }

    DeviceClient_disconnect(client);
    return 0;
}

