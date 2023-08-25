![Flomon](https://img.shields.io/badge/FLOMON-blue)
![C](https://img.shields.io/badge/C-green)
![SDK](https://img.shields.io/badge/SDK-black)
[![GitHub](https://img.shields.io/badge/license-EPL--1.0-FF0033.svg)](https://github.com/inforzia/iot-sdk-c/LICENSE)
# C Client SDK for the MQTT Protocol

The Flomon Device Client is an MQTT client library written in C for developing applications that run on the Linux.
This code is fully compatible with the [![Flomon](https://img.shields.io/badge/Paho-Project-blue)](https://github.com/orgs/eclipse/repositories?q=java+mqtt&type=all&language=&sort=)![Flomon](https://img.shields.io/badge/version-1.2.5-blue).

The Flomon Device Client provides APIs: MqttClient provides a fully asynchronous API where completion of activities is notified via registered callbacks. MqttClient is a synchronous wrapper around MqttAsyncClient where functions appear synchronous to the application.

## Usage
This SDK is used by registering it as an external library.
Go to the git repository [![SDK Repository](https://img.shields.io/badge/Inforzia%20SDK-Repo-green)](https://github.com/inforzia/iot-sdk-c) and clone the IOT-SDK project.
```less
$ git clone https://github.com/inforzia/iot-sdk-c.git
```
After navigating to the `iot-sdk-c` directory, grant execution permission to the `preRequirement.sh` file.
```less
$ cd iot-sdk-c/
$ sudo chmod +x preRequirement.sh
$ ./preRequirement.sh
```
shell script, the dependency libraries will be downloaded, and `mqttv3` will be installed in `/usr/local/lib/`.
Now, navigate back to the base directory and compile the `SDK` and `Sample Client`.
```less
$ gcc -c DeviceClient.c -o DeviceClient.o
$ gcc sample.c DeviceClient.o -o sample -lpaho-mqtt3c
```

This will create the sample executable file. Run the sample with your `server connection information` as parameters.
```less
$ ./sample [brokerUrl] [username] [password]
```


## IOT Device Sample Project
This `sample project` connects to the MQTT Broker of ![Flomon](https://img.shields.io/badge/Flomon-8A2BE2)(Inforzia IoT Platform) and sends data to the environmental sensor resource. To connect to Flomon and send data, the following prerequisites are required:

### Sample Code
```
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


```
### Prerequisites

1. Create an environmental sensor resource in Flomon.
2. Create a Flow in Flomon to receive MQTT messages.
3. Obtain the KEY from Flomon for secure MQTT connection.

### Execution Result

If you correctly input the server connection details, the URL of the connected broker will be printed, and the topic and message you specified will be published to the server.
In this `Sample project`, the `Temperature` and `Soil Moisture` data is sent to the `Flomon IoT Platform` only once.
```less
Broker URL : farm.inforzia.io:1883 connected.
Message published to topic: flomon/telemetry
Message : {"deviceKey":"env-B14","temperature":32.52,"soil_moisture":54.39}
```
