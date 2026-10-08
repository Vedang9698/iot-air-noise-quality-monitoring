import paho.mqtt.client as mqtt
import json


MQTT_BROKER = "localhost"
MQTT_PORT = 1883
MQTT_TOPIC = "esp32/+/sensors"


UBIDOTS_BROKER = "industrial.api.ubidots.com"
UBIDOTS_PORT = 1883
UBIDOTS_TOKEN = "BBUS-s38EavxfzPQ6vcvf57BNEoRmHbrpK9"
UBIDOTS_TOPIC = "/v1.6/devices/ridge-monitor"


ubidots_client = mqtt.Client()
ubidots_client.username_pw_set(UBIDOTS_TOKEN)

def on_ubidots_connect(client, userdata, flags, rc):
    if rc == 0:
        print("Connected to Ubidots")
    else:
        print(f"Ubidots connection failed. Return code: {rc}")

ubidots_client.on_connect = on_ubidots_connect


def on_connect(client, userdata, flags, rc):
    if rc == 0:
        print("Connected to MQTT broker")
        print(f"Subscribing to: {MQTT_TOPIC}")
        client.subscribe(MQTT_TOPIC)
    else:
        print(f"MQTT connection failed. Return code: {rc}")

def on_message(client, userdata, msg):
    try:
        payload = msg.payload.decode()
        data = json.loads(payload)

        print()
        print("========== ESP32 MESSAGE ==========")
        print(f"Topic: {msg.topic}")
        print(f"Message: {payload}")
        print("===================================")

        # Remove device_id before sending to Ubidots
        ubidots_data = {
            key: value
            for key, value in data.items()
            if key != "device_id"
        }

        # Send sensor data to Ubidots
        result = ubidots_client.publish(
            UBIDOTS_TOPIC,
            json.dumps(ubidots_data),
            qos=1
        )

        if result.rc == mqtt.MQTT_ERR_SUCCESS:
            print("Data published to Ubidots successfully")
            print(f"Ubidots payload: {json.dumps(ubidots_data)}")
        else:
            print(f"Ubidots publish failed. Return code: {result.rc}")

    except json.JSONDecodeError:
        print("Invalid JSON received from ESP32")
    except Exception as e:
        print(f"Error processing message: {e}")


print("Connecting to Ubidots...")

ubidots_client.connect(
    UBIDOTS_BROKER,
    UBIDOTS_PORT,
    60
)

ubidots_client.loop_start()


client = mqtt.Client()

client.on_connect = on_connect
client.on_message = on_message

print("Connecting to MQTT broker...")

client.connect(
    MQTT_BROKER,
    MQTT_PORT,
    60
)

client.loop_forever()