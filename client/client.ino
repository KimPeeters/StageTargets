#include <ESP8266WiFi.h>
#include <espnow.h>

const uint8_t CON_LED_PIN = 2;
// const uint8_t STATE_LED_PIN = 16;

const uint8_t STATE_INITIAL = 0;
const uint8_t STATE_IDLE = 1;
const uint8_t STATE_JOINING = 2;
const uint8_t STATE_ARMED = 3;

const uint8_t LED_OFF = 0;
const uint8_t LED_BLINK_SLOW = 1;
const uint8_t LED_BLINK_FAST = 2;
const uint8_t LED_ON = 3;

const uint8_t MSG_TYPE_ARN = 1;
const uint8_t MSG_TYPE_DISARM = 2;
const uint8_t MSG_TYPE_JOINED = 3;
const uint8_t MSG_TYPE_TRIGGERED = 4;
const uint8_t MSG_TYPE_JOIN = 5;

typedef struct struct_message_out
{
	uint8_t type;
	uint32_t duration;

} struct_message_out;

typedef struct struct_message_in
{
	uint8_t type;
} struct_message_in;

uint8_t broadcastAddress[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
uint8_t serverMac[6] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

uint8_t myMac[6] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

uint8_t state = STATE_INITIAL;

unsigned long armTime = 0;

uint8_t con_led_speed = LED_OFF;
uint8_t state_led_speed = LED_OFF;

uint32_t con_led_timer = 0;
uint32_t state_led_timer = 0;

bool con_led_state = false;
bool state_led_state = false;

struct_message_out myDataOut;
struct_message_in myDataIn;

uint16_t sensorReading = 0;

// TODO: REMOVE, TEMP AUTO TRIGGER
uint32_t triggerAt = 0;

void OnDataRecv(uint8_t *mac, uint8_t *incomingData, uint8_t len)
{
	memcpy(&myDataIn, incomingData, sizeof(myDataIn));

	Serial.print("Intype: ");
	Serial.println(myDataIn.type);

	switch (myDataIn.type)
	{
	case MSG_TYPE_ARN:
		if (state == STATE_IDLE)
		{
			Serial.println("Arm device");
			armTime = millis();
			state = STATE_ARMED;

			triggerAt = millis() + (random(30, 300) * 10);
		}
		break;
	case MSG_TYPE_DISARM:
		if (state == STATE_ARMED)
		{
			Serial.println("Disarm");
			armTime = 0;
			state = STATE_IDLE;
		}
		break;
	case MSG_TYPE_JOINED:
		if (state == STATE_JOINING)
		{
			randomSeed(analogRead(0));
			Serial.println("Joined");
			state = STATE_IDLE;

			memcpy(&serverMac, mac, 6);

			esp_now_add_peer(mac, ESP_NOW_ROLE_COMBO, 1, NULL, 0);
		}
		break;
	}
}

void setup()
{
	// Init Serial Monitor
	Serial.begin(115200);

	// Set device as a Wi-Fi Station
	WiFi.mode(WIFI_STA);

	// Init ESP-NOW
	if (esp_now_init() != 0)
	{
		Serial.println("Error initializing ESP-NOW");
		return;
	}

	getMac();

	// Once ESPNow is successfully Init, we will register for Send CB to
	// get the status of Trasnmitted packet
	esp_now_set_self_role(ESP_NOW_ROLE_COMBO);
	esp_now_register_recv_cb(OnDataRecv);
	// esp_now_register_send_cb(OnDataSent);

	// Register peer
	esp_now_add_peer(broadcastAddress, ESP_NOW_ROLE_COMBO, 1, NULL, 0);

	pinMode(CON_LED_PIN, OUTPUT);
	// pinMode(STATE_LED_PIN, OUTPUT);
}

uint8_t prevState = STATE_INITIAL;

uint32_t prevSample = 0;

void loop()
{
	if (prevState != state)
	{
		Serial.print("New state :");
		Serial.println(state);
		prevState = state;
	}

	// initial state, need to join
	if (state == STATE_INITIAL && millis() > 200)
	{
		// set con led to blink fast during joining
		con_led_speed = LED_BLINK_FAST;

		// set state to joining
		state = STATE_JOINING;

		// send data
		myDataOut.type = MSG_TYPE_JOIN;
		myDataOut.duration = 0;
		esp_now_send(broadcastAddress, (uint8_t *)&myDataOut, sizeof(myDataOut));
	}

	if (state == STATE_IDLE)
	{
		con_led_speed = LED_OFF;
		state_led_speed = LED_OFF;
	}

	// if is armed, keep reading trigger
	if (state == STATE_ARMED)
	{
		// if armed, set led on
		state_led_speed = LED_ON;
		con_led_speed = LED_ON;

		// if (millis() > triggerAt)
		// {
		// 	Serial.println("Sensor > 100");
		// 	myDataOut.type = MSG_TYPE_TRIGGERED;
		// 	myDataOut.duration = millis() - armTime;

		// 	state = STATE_IDLE;
		// 	esp_now_send(serverMac, (uint8_t *)&myDataOut, sizeof(myDataOut));
		// 	state_led_speed = LED_OFF;
		// }

		if (prevSample + 5 < millis())
		{
			sensorReading = analogRead(A0);
			if (sensorReading > 100)
			{
				Serial.println("Sensor > 100");
				myDataOut.type = MSG_TYPE_TRIGGERED;
				myDataOut.duration = millis() - armTime;

				state = STATE_IDLE;
				esp_now_send(serverMac, (uint8_t *)&myDataOut, sizeof(myDataOut));
				state_led_speed = LED_OFF;
			}
			prevSample = millis();
		}
	}

	// Handle led
	handleConLed();
}

void handleConLed()
{
	switch (con_led_speed)
	{
	case 0:
		digitalWrite(CON_LED_PIN, HIGH);
		break;
	case 1:
		if (millis() - con_led_timer > 1000)
		{
			con_led_state = !con_led_state;
			digitalWrite(CON_LED_PIN, con_led_state ? HIGH : LOW);
			con_led_timer = millis();
		}
		break;
	case 2:
		if (millis() - con_led_timer > 100)
		{
			con_led_state = !con_led_state;
			digitalWrite(CON_LED_PIN, con_led_state ? HIGH : LOW);
			con_led_timer = millis();
		}
		break;
	case 3:
		digitalWrite(CON_LED_PIN, LOW);
		break;
	}
}

void getMac()
{
	uint8_t bytes[6] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
	int values[6];

	String mac = WiFi.macAddress();
	const char *cstr = mac.c_str();

	if (6 == sscanf(cstr, "%x:%x:%x:%x:%x:%x%*c",
					&values[0], &values[1], &values[2],
					&values[3], &values[4], &values[5]))
	{
		/* convert to uint8_t */
		for (int i = 0; i < 6; ++i)
		{
			myMac[i] = (uint8_t)values[i];
		}
	}
}