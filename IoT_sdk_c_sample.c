#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "MQTTClient.h"
#include <time.h>

// Function to generate a random float between min and max
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

    MQTTClient client;
    MQTTClient_connectOptions conn_opts = MQTTClient_connectOptions_initializer;
    char client_id[] = "mqtt_sdk_c";
    char topic[] = "flomon/sdk/test";
    int rc;

    MQTTClient_create(&client, address, client_id, MQTTCLIENT_PERSISTENCE_NONE, NULL);
    conn_opts.keepAliveInterval = 20;
    conn_opts.cleansession = 1;
    conn_opts.username = username; // Set the username
    conn_opts.password = password; // Set the password

    if ((rc = MQTTClient_connect(client, &conn_opts)) != MQTTCLIENT_SUCCESS) {
        printf("Failed to connect, return code %d\n", rc);
        exit(EXIT_FAILURE);
    }

    // Seed the random number generator with current time
    srand(time(NULL));

    while (1) {
        // Generate random temperature and soil moisture values within the specified ranges
        float temperature = randomFloat(30.0, 33.0);
        float soil_moisture = randomFloat(54.0, 55.0);

        // Create a JSON-formatted message
        char message[256];
        snprintf(message, sizeof(message), "{\"deviceKey\":\"env-B14\",\"temperature\":%.2f,\"soil_moisture\":%.2f}", temperature, soil_moisture);

        MQTTClient_message pubmsg = MQTTClient_message_initializer;
        pubmsg.payload = message;
        pubmsg.payloadlen = strlen(message);
        pubmsg.qos = 1;
        pubmsg.retained = 0;

        MQTTClient_deliveryToken token;
        MQTTClient_publishMessage(client, topic, &pubmsg, &token);

        printf("Message published to topic: %s\n", topic);
	printf("Message : %s\n", message);

        // Wait for 1 second before publishing the next message
        sleep(1);
    }

    MQTTClient_disconnect(client, 10000);
    MQTTClient_destroy(&client);

    return 0;
}

