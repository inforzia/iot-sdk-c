[![Coverity Scan Build Status](https://scan.coverity.com/projects/2339/badge.svg)](https://scan.coverity.com/projects/paho-c)

# ![Flomon](https://img.shields.io/badge/FLOMON-blue) C Client Library for the MQTT Protocol

This repository contains the source code for the ![Flomon](https://img.shields.io/badge/FLOMON-blue) MQTT C client library.
This code is forked from [![Flomon](https://img.shields.io/badge/Paho-Project-blue)](https://github.com/eclipse/paho.mqtt.c)![Flomon](https://img.shields.io/badge/version-1.3.12-blue).
This code builds libraries which enable applications to connect to an [![MQTT](https://img.shields.io/badge/MQTT-green)](http://mqtt.org) broker to publish messages, and to subscribe to topics and receive published messages.

Synchronous and various asynchronous programming models are supported.

## Information About MQTT

* [MQTT website](http://mqtt.org)
* [The MQTT 3.1.1 standard](http://docs.oasis-open.org/mqtt/mqtt/v3.1.1/os/mqtt-v3.1.1-os.html)
* [The MQTT 5.0 standard](https://docs.oasis-open.org/mqtt/mqtt/v5.0/mqtt-v5.0.html)
* [HiveMQ introduction to MQTT](https://www.hivemq.com/mqtt/)
* [OASIS Introduction to MQTT presentation](https://www.oasis-open.org/committees/download.php/49205/MQTT-OASIS-Webinar.pdf)

## Libraries

The Inforzia C client comprises four variant libraries, shared or static:

 * paho-mqtt3a - asynchronous (MQTTAsync)
 * paho-mqtt3as - asynchronous with SSL/TLS (MQTTAsync)
 * paho-mqtt3c - "classic" / synchronous (MQTTClient)
 * paho-mqtt3cs - "classic" / synchronous with SSL/TLS (MQTTClient)

[Which Paho C API to use, with some history, for context](https://modelbasedtesting.co.uk/2013/10/13/which-paho-mqtt-c-api-to-use-and-some-history/)

## Usage and API

Detailed API documentation [is available online](https://eclipse.github.io/paho.mqtt.c/MQTTClient/html/).  It is also available by building the Doxygen docs in the  ``doc`` directory. 

Samples are available in the Doxygen docs and also in `src/samples` for reference.  These are:

- *paho_c_pub.c* and *paho_c_sub.c:* command line utilities to publish and subscribe, -h will give help
- *paho_cs_pub.c* and *paho_cs_sub.c:* command line utilities using MQTTClient to publish and subscribe
- *MQTTClient_publish.c, MQTTClient_subscribe.c* and *MQTTClient_publish_async.c:* MQTTClient simple code examples
- *MQTTAsync_publish.c* and *MQTTAsync_subscribe.c:* MQTTAsync simple code examples

Some potentially useful blog posts:

- [Paho client MQTT 5.0 support and command line utilities](https://modelbasedtesting.co.uk/2018/08/08/paho-c-client-mqtt-5-0-and-command-line-utilities/)
- [MQTT, QoS and persistence](https://modelbasedtesting.co.uk/2013/11/24/mqtt-qos-and-persistence/)
- [A story of MQTT 5.0](https://modelbasedtesting.co.uk/2018/04/09/a-story-of-mqtt-5-0/)

[Various MQTT and MQTT-SN talks I've given.](https://modelbasedtesting.co.uk/talks-ive-given/)

## Publication Example
```C
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "MQTTAsync.h"
 
#if !defined(_WIN32)
#include <unistd.h>
#else
#include <windows.h>
#endif
 
#if defined(_WRS_KERNEL)
#include <OsWrapper.h>
#endif
 
#define ADDRESS     "https://farm.inforzia.io:1883"
#define CLIENTID    "ExampleClientPub"
#define TOPIC       "flomon/telemetry"
#define PAYLOAD     "{\"deviceKey\":\"env-B11\", \"temperature\": 25.7, \"soil_moisture\": 69}"
#define QOS         1
#define TIMEOUT     10000L
 
int finished = 0;
 
void connlost(void *context, char *cause)
{
        MQTTAsync client = (MQTTAsync)context;
        MQTTAsync_connectOptions conn_opts = MQTTAsync_connectOptions_initializer;
        int rc;
 
        printf("\nConnection lost\n");
        printf("     cause: %s\n", cause);
 
        printf("Reconnecting\n");
        conn_opts.keepAliveInterval = 20;
        conn_opts.cleansession = 1;
        if ((rc = MQTTAsync_connect(client, &conn_opts)) != MQTTASYNC_SUCCESS)
        {
                printf("Failed to start connect, return code %d\n", rc);
                finished = 1;
        }
}
 
void onDisconnectFailure(void* context, MQTTAsync_failureData* response)
{
        printf("Disconnect failed\n");
        finished = 1;
}
 
void onDisconnect(void* context, MQTTAsync_successData* response)
{
        printf("Successful disconnection\n");
        finished = 1;
}
 
void onSendFailure(void* context, MQTTAsync_failureData* response)
{
        MQTTAsync client = (MQTTAsync)context;
        MQTTAsync_disconnectOptions opts = MQTTAsync_disconnectOptions_initializer;
        int rc;
 
        printf("Message send failed token %d error code %d\n", response->token, response->code);
        opts.onSuccess = onDisconnect;
        opts.onFailure = onDisconnectFailure;
        opts.context = client;
        if ((rc = MQTTAsync_disconnect(client, &opts)) != MQTTASYNC_SUCCESS)
        {
                printf("Failed to start disconnect, return code %d\n", rc);
                exit(EXIT_FAILURE);
        }
}
 
void onSend(void* context, MQTTAsync_successData* response)
{
        MQTTAsync client = (MQTTAsync)context;
        MQTTAsync_disconnectOptions opts = MQTTAsync_disconnectOptions_initializer;
        int rc;
 
        printf("Message with token value %d delivery confirmed\n", response->token);
        opts.onSuccess = onDisconnect;
        opts.onFailure = onDisconnectFailure;
        opts.context = client;
        if ((rc = MQTTAsync_disconnect(client, &opts)) != MQTTASYNC_SUCCESS)
        {
                printf("Failed to start disconnect, return code %d\n", rc);
                exit(EXIT_FAILURE);
        }
}
 
 
void onConnectFailure(void* context, MQTTAsync_failureData* response)
{
        printf("Connect failed, rc %d\n", response ? response->code : 0);
        finished = 1;
}
 
 
void onConnect(void* context, MQTTAsync_successData* response)
{
        MQTTAsync client = (MQTTAsync)context;
        MQTTAsync_responseOptions opts = MQTTAsync_responseOptions_initializer;
        MQTTAsync_message pubmsg = MQTTAsync_message_initializer;
        int rc;
 
        printf("Successful connection\n");
        opts.onSuccess = onSend;
        opts.onFailure = onSendFailure;
        opts.context = client;
        pubmsg.payload = PAYLOAD;
        pubmsg.payloadlen = (int)strlen(PAYLOAD);
        pubmsg.qos = QOS;
        pubmsg.retained = 0;
        if ((rc = MQTTAsync_sendMessage(client, TOPIC, &pubmsg, &opts)) != MQTTASYNC_SUCCESS)
        {
                printf("Failed to start sendMessage, return code %d\n", rc);
                exit(EXIT_FAILURE);
        }
}
 
int messageArrived(void* context, char* topicName, int topicLen, MQTTAsync_message* m)
{
        // not expecting any messages
        return 1;
}
 
int main(int argc, char* argv[])
{
        MQTTAsync client;
        MQTTAsync_connectOptions conn_opts = MQTTAsync_connectOptions_initializer;
        int rc;
 
        if ((rc = MQTTAsync_create(&client, ADDRESS, CLIENTID, MQTTCLIENT_PERSISTENCE_NONE, NULL)) != MQTTASYNC_SUCCESS)
        {
                printf("Failed to create client object, return code %d\n", rc);
                exit(EXIT_FAILURE);
        }
 
        if ((rc = MQTTAsync_setCallbacks(client, NULL, connlost, messageArrived, NULL)) != MQTTASYNC_SUCCESS)
        {
                printf("Failed to set callback, return code %d\n", rc);
                exit(EXIT_FAILURE);
        }
 
        conn_opts.keepAliveInterval = 20;
        conn_opts.cleansession = 1;
        conn_opts.onSuccess = onConnect;
        conn_opts.onFailure = onConnectFailure;
        conn_opts.context = client;
        if ((rc = MQTTAsync_connect(client, &conn_opts)) != MQTTASYNC_SUCCESS)
        {
                printf("Failed to start connect, return code %d\n", rc);
                exit(EXIT_FAILURE);
        }
 
        printf("Waiting for publication of %s\n"
         "on topic %s for client with ClientID: %s\n",
         PAYLOAD, TOPIC, CLIENTID);
        while (!finished)
                #if defined(_WIN32)
                        Sleep(100);
                #else
                        usleep(10000L);
                #endif
 
        MQTTAsync_destroy(&client);
        return rc;
}
```

## Subscription Example
```C
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "MQTTAsync.h"
 
#if !defined(_WIN32)
#include <unistd.h>
#else
#include <windows.h>
#endif
 
#if defined(_WRS_KERNEL)
#include <OsWrapper.h>
#endif
 
#define ADDRESS     "tcp://mqtt.eclipseprojects.io:1883"
#define CLIENTID    "ExampleClientSub"
#define TOPIC       "MQTT Examples"
#define PAYLOAD     "Hello World!"
#define QOS         1
#define TIMEOUT     10000L
 
int disc_finished = 0;
int subscribed = 0;
int finished = 0;
 
void connlost(void *context, char *cause)
{
        MQTTAsync client = (MQTTAsync)context;
        MQTTAsync_connectOptions conn_opts = MQTTAsync_connectOptions_initializer;
        int rc;
 
        printf("\nConnection lost\n");
        if (cause)
                printf("     cause: %s\n", cause);
 
        printf("Reconnecting\n");
        conn_opts.keepAliveInterval = 20;
        conn_opts.cleansession = 1;
        if ((rc = MQTTAsync_connect(client, &conn_opts)) != MQTTASYNC_SUCCESS)
        {
                printf("Failed to start connect, return code %d\n", rc);
                finished = 1;
        }
}
 
 
int msgarrvd(void *context, char *topicName, int topicLen, MQTTAsync_message *message)
{
    printf("Message arrived\n");
    printf("     topic: %s\n", topicName);
    printf("   message: %.*s\n", message->payloadlen, (char*)message->payload);
    MQTTAsync_freeMessage(&message);
    MQTTAsync_free(topicName);
    return 1;
}
 
void onDisconnectFailure(void* context, MQTTAsync_failureData* response)
{
        printf("Disconnect failed, rc %d\n", response->code);
        disc_finished = 1;
}
 
void onDisconnect(void* context, MQTTAsync_successData* response)
{
        printf("Successful disconnection\n");
        disc_finished = 1;
}
 
void onSubscribe(void* context, MQTTAsync_successData* response)
{
        printf("Subscribe succeeded\n");
        subscribed = 1;
}
 
void onSubscribeFailure(void* context, MQTTAsync_failureData* response)
{
        printf("Subscribe failed, rc %d\n", response->code);
        finished = 1;
}
 
 
void onConnectFailure(void* context, MQTTAsync_failureData* response)
{
        printf("Connect failed, rc %d\n", response->code);
        finished = 1;
}
 
 
void onConnect(void* context, MQTTAsync_successData* response)
{
        MQTTAsync client = (MQTTAsync)context;
        MQTTAsync_responseOptions opts = MQTTAsync_responseOptions_initializer;
        int rc;
 
        printf("Successful connection\n");
 
        printf("Subscribing to topic %s\nfor client %s using QoS%d\n\n"
           "Press Q<Enter> to quit\n\n", TOPIC, CLIENTID, QOS);
        opts.onSuccess = onSubscribe;
        opts.onFailure = onSubscribeFailure;
        opts.context = client;
        if ((rc = MQTTAsync_subscribe(client, TOPIC, QOS, &opts)) != MQTTASYNC_SUCCESS)
        {
                printf("Failed to start subscribe, return code %d\n", rc);
                finished = 1;
        }
}
 
 
int main(int argc, char* argv[])
{
        MQTTAsync client;
        MQTTAsync_connectOptions conn_opts = MQTTAsync_connectOptions_initializer;
        MQTTAsync_disconnectOptions disc_opts = MQTTAsync_disconnectOptions_initializer;
        int rc;
        int ch;
 
        if ((rc = MQTTAsync_create(&client, ADDRESS, CLIENTID, MQTTCLIENT_PERSISTENCE_NONE, NULL))
                        != MQTTASYNC_SUCCESS)
        {
                printf("Failed to create client, return code %d\n", rc);
                rc = EXIT_FAILURE;
                goto exit;
        }
 
        if ((rc = MQTTAsync_setCallbacks(client, client, connlost, msgarrvd, NULL)) != MQTTASYNC_SUCCESS)
        {
                printf("Failed to set callbacks, return code %d\n", rc);
                rc = EXIT_FAILURE;
                goto destroy_exit;
        }
 
        conn_opts.keepAliveInterval = 20;
        conn_opts.cleansession = 1;
        conn_opts.onSuccess = onConnect;
        conn_opts.onFailure = onConnectFailure;
        conn_opts.context = client;
        if ((rc = MQTTAsync_connect(client, &conn_opts)) != MQTTASYNC_SUCCESS)
        {
                printf("Failed to start connect, return code %d\n", rc);
                rc = EXIT_FAILURE;
                goto destroy_exit;
        }
 
        while (!subscribed && !finished)
                #if defined(_WIN32)
                        Sleep(100);
                #else
                        usleep(10000L);
                #endif
 
        if (finished)
                goto exit;
 
        do
        {
                ch = getchar();
        } while (ch!='Q' && ch != 'q');
 
        disc_opts.onSuccess = onDisconnect;
        disc_opts.onFailure = onDisconnectFailure;
        if ((rc = MQTTAsync_disconnect(client, &disc_opts)) != MQTTASYNC_SUCCESS)
        {
                printf("Failed to start disconnect, return code %d\n", rc);
                rc = EXIT_FAILURE;
                goto destroy_exit;
        }
        while (!disc_finished)
        {
                #if defined(_WIN32)
                        Sleep(100);
                #else
                        usleep(10000L);
                #endif
        }
 
destroy_exit:
        MQTTAsync_destroy(&client);
exit:
        return rc;
}
```

## Runtime tracing

A number of environment variables control runtime tracing of the C library.

Tracing is switched on using `MQTT_C_CLIENT_TRACE` (a value of ON traces to stdout, any other value should specify a file to trace to).

The verbosity of the output is controlled using the  `MQTT_C_CLIENT_TRACE_LEVEL` environment variable - valid values are ERROR, PROTOCOL, MINIMUM, MEDIUM and MAXIMUM (from least to most verbose).

The variable `MQTT_C_CLIENT_TRACE_MAX_LINES` limits the number of lines of trace that are output.

```
export MQTT_C_CLIENT_TRACE=ON
export MQTT_C_CLIENT_TRACE_LEVEL=PROTOCOL
```
## Building with CMake

The build process currently supports a number of Linux "flavors" including ARM and s390, OS X, AIX and Solaris as well as the Windows operating system. The build process requires the following tools:
  * [CMake](http://cmake.org)
  * [GNU Make](https://www.gnu.org/software/make/) or [Ninja](https://martine.github.io/ninja/)
  * A conforming C compiler, such as [gcc](https://gcc.gnu.org/), [Clang](https://clang.llvm.org/), etc

On Debian based systems this would mean that the following packages have to be installed:

```
$ apt-get install build-essential gcc make cmake cmake-gui cmake-curses-gui
```

Also, in order to build a debian package from the source code, the following packages have to be installed

```
$ apt-get install fakeroot devscripts dh-make lsb-release
```

Ninja can be downloaded from its github project page in the "releases" section. Optionally it is possible to build binaries with SSL/TLS support. This requires the OpenSSL libraries and includes to be available. E. g. on Debian:

```
$ apt-get install libssl-dev
```

The documentation requires doxygen and optionally graphviz:

```
$ apt-get install doxygen graphviz
```

### Building your application with CMake

If the Paho C library was built with CMake and is already installed on the system, it is relatively easy to set up a CMake build for your application. (If it's not already built and installed read the next section).

The library can be built with several options which create variations of the library for asynchronous or synchronous use; encryption (SSL/TLS) support or not; and whether the library is shared or static. CMake exports all of the libraries that were built as targets, and the user can chose which is best suited for an application.

The package is named: **eclipse-paho-mqtt-c**

The namespace for all the targets is also: **eclipse-paho-mqtt-c**

The target names are the same as the library names. The static libraries append *-static* to the target name even for platforms that use the same base name for shared and static libraries. So:

| Target              | Description                                       |
|--------------------|---------------------------------------------------|
| paho-mqtt3a         | asynchronous, no encryption                      |
| paho-mqtt3as        | asynchronous with SSL/TLS support                |
| paho-mqtt3c         | synchronous, no encryption                       |
| paho-mqtt3cs        | synchronous with SSL/TLS support                 |
| paho-mqtt3a-static  | asynchronous, no encryption, static linkage      |
| paho-mqtt3as-static | asynchronous with SSL/TLS support, static linkage|
| paho-mqtt3c-static  | synchronous, no encryption, static linkage       |
| paho-mqtt3cs-static | synchronous with SSL/TLS support, static linkage |

Remember, though, that not all of these targets may be available. It depends on how the library was built.

A sample *CMakeLists.txt* for an application that uses the asynchronous library with encryption support *(paho-mqtt3as)* might look like this:

```
cmake_minimum_required(VERSION 3.5)
project(MyMQTTApp VERSION 1.0.0 LANGUAGES C)

find_package(eclipse-paho-mqtt-c REQUIRED)

add_executable(MyMQTTApp MyMQTTApp.c)
target_link_libraries(MQTTVersion eclipse-paho-mqtt-c::paho-mqtt3as) 
```

If the library was installed to a non-traditional location, you may need to tell CMake where to find it using `CMAKE_PREFIX_PATH`. For example, if you installed it in */opt/mqtt/paho.mqtt.c*

```
$ cmake -DCMAKE_PREFIX_PATH=/opt/mqtt/paho.mqtt.c ..
```

### Building the Paho C library with CMake

Before compiling, determine the value of some variables in order to configure features, library locations, and other options:

| Variable             | Default Value     | Description                                                                                     |
|----------------------|-------------------|-------------------------------------------------------------------------------------------------|
| PAHO_BUILD_SHARED    | TRUE              | Build a shared version of the libraries                                                        |
| PAHO_BUILD_STATIC    | FALSE             | Build a static version of the libraries                                                        |
| PAHO_HIGH_PERFORMANCE| FALSE             | When set to true, the debugging aids internal tracing and heap tracking are not included.      |
| PAHO_WITH_SSL        | FALSE             | Flag that defines whether to build ssl-enabled binaries too.                                    |
| OPENSSL_ROOT_DIR     | "" (system default)| Directory containing your OpenSSL installation (i.e. `/usr/local` when headers are in `/usr/local/include` and libraries are in `/usr/local/lib`) |
| PAHO_BUILD_DOCUMENTATION| FALSE          | Create and install the HTML based API documentation (requires Doxygen)                         |
| PAHO_BUILD_SAMPLES  | FALSE             | Build sample programs                                                                          |
| PAHO_ENABLE_TESTING | TRUE              | Build test and run                                                                             |
| MQTT_TEST_BROKER     | tcp://localhost:1883 | MQTT connection URL for a broker to use during test execution                               |
| MQTT_TEST_PROXY      | tcp://localhost:1883 | Hostname of the test proxy to use                                                              |
| MQTT_SSL_HOSTNAME    | localhost         | Hostname of a test SSL MQTT broker to use                                                       |
| PAHO_BUILD_DEB_PACKAGE| FALSE            | Build debian package                                                                            |

Using these variables CMake can be used to generate your Ninja or Make files. Using CMake, building out-of-source is the default. Therefore it is recommended to invoke all build commands inside your chosen build directory but outside of the source tree.

An example build session targeting the build platform could look like this:

```
$ mkdir /tmp/build.paho ; cd /tmp/build.paho
$ cmake -DPAHO_WITH_SSL=TRUE -DPAHO_BUILD_DOCUMENTATION=TRUE \
    -DPAHO_BUILD_SAMPLES=TRUE ~/paho.mqtt.c
```

Invoking cmake and specifying build options can also be performed using cmake-gui or ccmake (see https://cmake.org/runningcmake/). For example:

```
$ ccmake ~/paho.mqtt.c
```

To compile/link the binaries, to install, or to generate packages, use these commands:

```
$ cmake --build .

$ cmake --build . --target install

$ cmake --build . --target package
```

To build, install, or generate packages, you can also use the generated builder like _ninja_ or _make_ directly after invoking the initial CMake configuration step, such as `ninja package` or `make -j <number-of-jpbs> package`.

### Debug builds

Debug builds can be performed by defining the value of the `CMAKE_BUILD_TYPE` option to `Debug`. For example:

```
$ cmake -DCMAKE_BUILD_TYPE=Debug ~/paho.mqtt.c
```

### Running the tests

Test code is available in the `test` directory. The tests can be built and executed with the CMake build system. The test execution requires a MQTT broker running. By default, the build system uses `localhost`, however it is possible to configure the build to use an external broker. These parameters are documented in the Build Requirements section above.

After ensuring a MQTT broker is available, it is possible to execute the tests by starting the proxy and running `ctest` as described below:

```
$ python ../test/mqttsas2.py &
$ ctest -VV
```

### Cross compilation

Cross compilation using CMake is performed by using so called "toolchain files" (see: http://www.vtk.org/Wiki/CMake_Cross_Compiling).

The path to the toolchain file can be specified by using CMake's `-DCMAKE_TOOLCHAIN_FILE` option. In case no toolchain file is specified, the build is performed for the native build platform.

For your convenience toolchain files for the following platforms can be found in the `cmake` directory of Eclipse Paho:
  * Linux x86
  * Linux ARM11 (a.k.a. the Raspberry Pi)
  * Windows x86_64
  * Windows x86

The provided toolchain files assume that required compilers/linkers are to be found in the environment, i. e. the PATH-Variable of your user or system. If you prefer, you can also specify the absolute location of your compilers in the toolchain files.

Example invocation for the Raspberry Pi:

```
$ cmake -GNinja -DPAHO_WITH_SSL=TRUE -DPAHO_BUILD_SAMPLES=TRUE \
    -DPAHO_BUILD_DOCUMENTATION=TRUE \
    -DOPENSSL_LIB_SEARCH_PATH=/tmp/libssl-dev/usr/lib/arm-linux-gnueabihf \
    -DOPENSSL_INC_SEARCH_PATH="/tmp/libssl-dev/usr/include/openssl;/tmp/libssl-dev/usr/include/arm-linux-gnueabihf" \
    -DCMAKE_TOOLCHAIN_FILE=~/paho.mqtt.c/cmake/toolchain.linux-arm11.cmake \
    ~/paho.mqtt.c
```

Compilers for the Raspberry Pi and other ARM targets can be obtained from ARM (https://developer.arm.com/tools-and-software/open-source-software/developer-tools/gnu-toolchain/downloads)

This example assumes that OpenSSL-libraries and includes have been installed in the `/tmp/libssl-dev` directory.

Example invocation for Windows 64 bit:

```
$ cmake -DPAHO_BUILD_SAMPLES=TRUE \
    -DCMAKE_TOOLCHAIN_FILE=~/paho.mqtt.c/cmake/toolchain.win64.cmake \
    ~/paho.mqtt.c
```

In this case the libraries and executable are not linked against OpenSSL Libraries. Cross compilers for the Windows platform can be installed on Debian like systems like this:

```
$ apt-get install gcc-mingw-w64-x86-64 gcc-mingw-w64-i686
```

## Build instructions for GNU Make

Ensure the OpenSSL development package is installed.  Then from the client library base directory run:

```
$ make
$ sudo make install
```

This will build and install the libraries.  To uninstall:

```
$ sudo make uninstall
```

To build the documentation requires doxygen and optionally graphviz.

```
$ make html
```

The provided GNU Makefile is intended to perform all build steps in the ```build``` directory within the source-tree of Eclipse Paho. Generated binares, libraries, and the documentation can be found in the ```build/output``` directory after completion. 

Options that are passed to the compiler/linker can be specified by typical Unix build variables:

| Variable | Description               |
|----------|---------------------------|
| CC       | Path to the C compiler    |
| CFLAGS   | Flags passed to compiler calls |
| LDFLAGS  | Flags passed to linker calls |

## Building paho-mqtt - Using vcpkg

You can download and install paho-mqtt using the [vcpkg](https://github.com/Microsoft/vcpkg) dependency manager:

    git clone https://github.com/Microsoft/vcpkg.git
    cd vcpkg
    ./bootstrap-vcpkg.sh
    ./vcpkg integrate install
    ./vcpkg install paho-mqtt

The paho-mqtt port in vcpkg is kept up to date by Microsoft team members and community contributors. If the version is out of date, please [create an issue or pull request](https://github.com/Microsoft/vcpkg) on the vcpkg repository.
