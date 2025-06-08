#ifndef MQTT_EXAMPLE_H
#define MQTT_EXAMPLE_H

#ifdef __cplusplus
extern "C" {
#endif

// MQTT example initialization and functions
void mqtt_example_init(void);
void mqtt_example_publish_test_message(void);
void mqtt_example_publish_sensor_data(float temperature, float humidity);

#ifdef __cplusplus
}
#endif

#endif  // MQTT_EXAMPLE_H