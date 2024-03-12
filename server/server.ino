#include <ESP8266WiFi.h>
#include <espnow.h>

const uint8_t CON_LED_PIN = 2;
const uint8_t STATE_LED_PIN = 16;

const uint8_t BUTTON_1_PIN = 0;
const uint8_t BUTTON_2_PIN = 4;

const uint8_t BLEEP_PIN = 5;

const uint8_t STATE_INITIAL = 0;
const uint8_t STATE_IDLE = 1;
const uint8_t STATE_ALLOW_JOIN = 2;
const uint8_t STATE_RUN_PROGRAM = 3;

const uint8_t LED_OFF = 0;
const uint8_t LED_BLINK_SLOW = 1;
const uint8_t LED_BLINK_FAST = 2;
const uint8_t LED_ON = 3;

const uint8_t MSG_TYPE_ARM = 1;
const uint8_t MSG_TYPE_DISARM = 2;
const uint8_t MSG_TYPE_JOINED = 3;
const uint8_t MSG_TYPE_TRIGGERED = 4;
const uint8_t MSG_TYPE_JOIN = 5;

const uint8_t MENU_OFF = 0;
const uint8_t MENU_ITEM_PROGRAM_1 = 1;
const uint8_t MENU_ITEM_REMOVE_ALL_DEVICES = 2;

const uint8_t PROGRAM_STATE_IDLE = 0;
const uint8_t PROGRAM_STATE_RUNING = 1;
const uint8_t PROGRAM_STATE_FINISHED = 2;

typedef struct struct_message_out
{
	uint8_t type;
} struct_message_out;

typedef struct struct_message_in
{
	uint8_t type;
	uint32_t duration;

} struct_message_in;

uint8_t broadcastAddress[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

struct_message_out myDataOut;
struct_message_in myDataIn;

uint8_t state = STATE_INITIAL;

uint32_t armTime = 0;

uint8_t con_led_speed = LED_OFF;
uint8_t state_led_speed = LED_OFF;

uint32_t con_led_timer = 0;
uint32_t state_led_timer = 0;

bool con_led_state = false;
bool state_led_state = false;

/**
 * Button helper variables
 */
uint8_t button1State = 1;
uint16_t button1DebounceTime = 0;
uint32_t lastButton1PressTime = 0;
uint16_t lastButton1Duration = 0;
uint32_t lastButton1Release = 0;

uint8_t button2State = 1;
uint16_t button2DebounceTime = 0;
uint32_t lastButton2PressTime = 0;
uint16_t lastButton2Duration = 0;
uint32_t lastButton2Release = 0;

/**
 * Menu variables
 */

uint8_t menuItem = MENU_OFF;

/**
 * Program variables
 */
uint8_t program1_state = PROGRAM_STATE_IDLE;
uint32_t program1_armTime = 0;
uint16_t program1_timeout = 10000;
uint16_t program1_responseTimes[10] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

/**
 * Joined targets
 */
uint8_t joinedTargets[10][6] = {
	{0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
	{0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
	{0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
	{0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
	{0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
	{0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
	{0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
	{0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
	{0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
	{0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
};

/**
 * Bleep
 */
uint32_t bleepSoundUntil = 0;
bool bleepActive = false;

void OnDataRecv(uint8_t *mac, uint8_t *incomingData, uint8_t len)
{
	memcpy(&myDataIn, incomingData, sizeof(myDataIn));

	switch (myDataIn.type)
	{
	case MSG_TYPE_TRIGGERED:
		onTargetTriggered(mac);

		break;
	case MSG_TYPE_JOIN:
		onTargetJoin(mac);
		break;
	}
}

void OnDataSent(unsigned char *a, unsigned char b)
{
	// Serial.print("\r\nLast Packet Send Mac:\t");
	// int s = sizeof(a);
	// for (int i = 0; i < 6; i++)
	// {
	// 	Serial.print(a[i]);
	// 	Serial.print(" ");
	// }
	// Serial.print("\r\nLast Packet Send Status:\t");
	// Serial.println(b);
}

void setup()
{
	Serial.begin(115200);

	WiFi.mode(WIFI_STA);

	if (esp_now_init() != 0)
	{
		Serial.println("Error initializing ESP-NOW");
		return;
	}

	esp_now_set_self_role(ESP_NOW_ROLE_COMBO);
	esp_now_register_send_cb(OnDataSent);
	esp_now_register_recv_cb(OnDataRecv);

	esp_now_add_peer(broadcastAddress, ESP_NOW_ROLE_COMBO, 1, NULL, 0);

	// Button
	pinMode(BUTTON_1_PIN, INPUT_PULLUP);
	pinMode(BUTTON_2_PIN, INPUT_PULLUP);

	pinMode(CON_LED_PIN, OUTPUT);
	pinMode(STATE_LED_PIN, OUTPUT);

	pinMode(BLEEP_PIN, OUTPUT);

	state = STATE_IDLE;
}

uint8_t prevState = STATE_INITIAL;

uint32_t lastSend = 0;
bool sendType = false;

void loop()
{
	if (prevState != state)
	{
		Serial.print("New state :");
		Serial.println(state);
		prevState = state;
	}

	// handle buttonpress
	registerButtonPress();

	handleShortButton1Press();
	handleLongButton1Press();
	lastButton1Release = 0;
	lastButton1Duration = 0;

	handleShortButton2Press();
	handleLongButton2Press();
	lastButton2Release = 0;
	lastButton2Duration = 0;

	handleBleep();

	handleProgram();

	handleShowMenu();

	// Handle leds
	handleConLed();
	handleStateLed();
}

void handleStateLed()
{

	switch (state_led_speed)
	{
	case LED_OFF:
		digitalWrite(STATE_LED_PIN, HIGH);
		break;
	case LED_BLINK_SLOW:
		if (millis() - state_led_timer > 1000)
		{
			state_led_state = !state_led_state;
			digitalWrite(STATE_LED_PIN, state_led_state ? HIGH : LOW);
			state_led_timer = millis();
		}
		break;
	case LED_BLINK_FAST:
		if (millis() - state_led_timer > 100)
		{
			state_led_state = !state_led_state;
			digitalWrite(STATE_LED_PIN, state_led_state ? HIGH : LOW);
			con_led_timer = millis();
		}
		break;
	case LED_ON:
		digitalWrite(STATE_LED_PIN, LOW);
		break;
	}
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

void registerButtonPress()
{

	// Detect buttonPress
	if (button1DebounceTime < millis())
	{
		int newBtnState = digitalRead(BUTTON_1_PIN);
		if (button1State != newBtnState)
		{
			// debounce time 50ms
			button1DebounceTime = millis() + 50;
			button1State = newBtnState;
			if (button1State == 0)
			{
				lastButton1PressTime = millis();
			}
			if (newBtnState == 1)
			{
				lastButton1Duration = millis() - lastButton1PressTime;
				lastButton1Release = millis();
				lastButton1PressTime = 0;

				Serial.print("Button 1 released after ");
				Serial.print(lastButton1Duration);
				Serial.println(" ms");
			}
		}
	}

	if (button2DebounceTime < millis())
	{
		int newBtnState = digitalRead(BUTTON_2_PIN);
		if (button2State != newBtnState)
		{
			// debounce time 50ms
			button2DebounceTime = millis() + 50;
			button2State = newBtnState;
			if (button2State == 0)
			{
				lastButton2PressTime = millis();
			}
			if (newBtnState == 1)
			{
				lastButton2Duration = millis() - lastButton2PressTime;
				lastButton2Release = millis();
				lastButton2PressTime = 0;

				Serial.print("Button 2 released after ");
				Serial.print(lastButton2Duration);
				Serial.println(" ms");
			}
		}
	}

	// When buttonpress is not handled within 100 ms, ignore.
	if (lastButton1Release != 0 && lastButton1Release + 100 < millis())
	{
		lastButton1Release = 0;
		lastButton1Duration = 0;
	}

	if (lastButton2Release != 0 && lastButton2Release + 100 < millis())
	{
		lastButton2Release = 0;
		lastButton2Duration = 0;
	}
}

void handleShortButton1Press()
{

	if (lastButton1Release > 0 && lastButton1Duration < 1000)
	{
		switch (state)
		{
		case STATE_ALLOW_JOIN:
			state = STATE_IDLE;
			con_led_speed = LED_OFF;
			break;
		case STATE_IDLE:
			menuItem++;
			if (menuItem > MENU_ITEM_REMOVE_ALL_DEVICES)
			{
				menuItem = 0;
			}
			break;
		}
	}
}

void handleLongButton1Press()
{

	if (lastButton1Release > 0 && lastButton1Duration > 1000 && state == STATE_IDLE)
	{
		// short press, start arm
		Serial.println("Enabled join mode");
		state = STATE_ALLOW_JOIN;

		con_led_speed = LED_BLINK_FAST;
	}
}

void handleShortButton2Press()
{

	if (lastButton2Release > 0 && lastButton2Duration < 1000)
	{

		if (state == STATE_RUN_PROGRAM && program1_state == PROGRAM_STATE_FINISHED)
		{
			program1_armTime = 0;
			for (uint8_t i = 0; i < 10; i++)
			{
				program1_responseTimes[i] = 0;
			}
			program1_state = PROGRAM_STATE_IDLE;
		}

		if (state == STATE_IDLE)
		{
			// go to next menu item
			switch (menuItem)
			{
			case MENU_OFF:
				state = STATE_IDLE;
				break;

			case MENU_ITEM_PROGRAM_1:
				state = STATE_RUN_PROGRAM;
				break;

			case MENU_ITEM_REMOVE_ALL_DEVICES:
				for (int i = 0; i < 10; i++)
				{
					for (int j = 0; j < 6; j++)
					{
						joinedTargets[i][j] = 0x00;
					}
				}
				break;
			}
		}
	}
}

void handleLongButton2Press()
{

	if (lastButton2Release > 0 && lastButton2Duration > 1000)
	{
	}
}

uint8_t prevMenu = 0;
void handleShowMenu()
{

	if (menuItem != prevMenu)
	{
		switch (menuItem)
		{
		case MENU_OFF:
			Serial.println("MENU: MENU_OFF");
			break;
		case MENU_ITEM_PROGRAM_1:
			Serial.println("MENU: MENU_ITEM_PROGRAM_1");
			break;
		case MENU_ITEM_REMOVE_ALL_DEVICES:
			Serial.println("MENU: MENU_ITEM_REMOVE_ALL_DEVICES");
			break;
		}
		prevMenu = menuItem;
	}
}

uint32 runningProgram = 0;
void handleProgram()
{
	if (state != STATE_RUN_PROGRAM)
	{
		return;
	}

	if (runningProgram != menuItem)
	{
		Serial.print("Run program: ");
		Serial.println(menuItem);
		runningProgram = menuItem;
	}

	if (menuItem == MENU_ITEM_PROGRAM_1)
	{
		handleProgram1();
	}
}

/**
 * Program 1
 *
 * Arm all targets at the same time
 * Show time to hit all targets.
 */

void handleProgram1()
{
	if (program1_state == PROGRAM_STATE_RUNING && program1_armTime + program1_timeout < millis())
	{
		// timeout, reset program
		Serial.println("Program 1 timeout!");
		disarmAllTargets();
		program1_armTime = 0;
		program1_state = PROGRAM_STATE_FINISHED;
	}

	if (program1_state == PROGRAM_STATE_IDLE)
	{
		Serial.println("Program 1 arm!");
		program1_armTime = millis();
		armAllTargets();
		program1_state = PROGRAM_STATE_RUNING;
		bleepSoundUntil = millis() + 200;
	}

	if (program1_state == PROGRAM_STATE_RUNING)
	{
		bool done = true;
		uint16_t firstHit = 0;
		uint16_t lastHit = 0;

		for (uint8_t i = 0; i < 10; i++)
		{

			if (joinedTargets[i][0] == 0x00)
			{
				continue;
			}

			if (program1_responseTimes[i] == 0)
			{
				done = false;
				continue;
			}

			if (firstHit == 0 || firstHit > program1_responseTimes[i])
			{
				firstHit = program1_responseTimes[i];
			}
			if (lastHit == 0 || lastHit < program1_responseTimes[i])
			{
				lastHit = program1_responseTimes[i];
			}
		}

		if (done)
		{
			program1_state = PROGRAM_STATE_FINISHED;

			Serial.println("RESULT:");
			Serial.print("First: ");
			Serial.println(firstHit);
			Serial.print("Last: ");
			Serial.println(lastHit);
		}
	}
}

void onTargetTriggered(uint8_t *mac)
{
	if (state == STATE_RUN_PROGRAM)
	{
		switch (menuItem)
		{
		case MENU_ITEM_PROGRAM_1:
			onTargetTriggered_program1(mac);
			break;
		}
	}
}

void onTargetTriggered_program1(uint8_t *mac)
{

	uint32_t responseTime = millis() - program1_armTime;
	for (uint8_t i = 0; i < 10; i++)
	{
		if (joinedTargets[i][0] == mac[0] &&
			joinedTargets[i][1] == mac[1] &&
			joinedTargets[i][2] == mac[2] &&
			joinedTargets[i][3] == mac[3] &&
			joinedTargets[i][4] == mac[4] &&
			joinedTargets[i][5] == mac[5])
		{
			program1_responseTimes[i] = responseTime;
			Serial.print("Hit target ");
			Serial.println(i);
		}
	}
}

void disarmAllTargets()
{
	myDataOut.type = MSG_TYPE_DISARM;
	for (uint8_t i = 0; i < 10; i++)
	{
		if (joinedTargets[i][0] != 0x00)
		{
			esp_now_send(joinedTargets[i], (uint8_t *)&myDataOut, sizeof(myDataOut));
		}
	}
}

void armAllTargets()
{
	myDataOut.type = MSG_TYPE_ARM;
	for (uint8_t i = 0; i < 10; i++)
	{
		if (joinedTargets[i][0] != 0x00)
		{
			esp_now_send(joinedTargets[i], (uint8_t *)&myDataOut, sizeof(myDataOut));
		}
	}
}

void handleBleep()
{

	if (!bleepActive && bleepSoundUntil > millis())
	{
		digitalWrite(BLEEP_PIN, HIGH);
		bleepActive = true;
	}

	if (bleepActive && bleepSoundUntil < millis())
	{
		digitalWrite(BLEEP_PIN, LOW);
		bleepSoundUntil = 0;
		bleepActive = false;
	}
}

void onTargetJoin(uint8_t *mac)
{
	if (state == STATE_ALLOW_JOIN)
	{
		// check if target is already know
		for (int i = 0; i < 10; i++)
		{
			bool found = true;
			for (int j = 0; j < 6; j++)
			{
				if (joinedTargets[i][j] != mac[j])
				{
					found = false;
				}
			}

			// if target is known, remove it so we can add it again
			if (found)
			{
				for (int j = 0; j < 6; j++)
				{
					joinedTargets[i][j] = 0x00;
				}
			}
		}

		bool done = false;
		// find first free slot and add target
		for (int i = 0; i < 10; i++)
		{
			if (!done)
			{
				if (joinedTargets[i][0] == 0x00)
				{
					// for (int j = 0; j < 6; j++)
					// {
					// 	joinedTargets[i][j] = mac[j];
					// }
					memcpy(joinedTargets[i], mac, 6);
					esp_now_add_peer(mac, ESP_NOW_ROLE_COMBO, 1, NULL, 0);

					// TODO: replace delay for something non blocking
					delay(50);

					Serial.print("Target added at ");
					Serial.println(i);

					myDataOut.type = MSG_TYPE_JOINED;
					esp_now_send(mac, (uint8_t *)&myDataOut, sizeof(myDataOut));
					done = true;
				}
			}
		}

		// Serial.println("Mac table: ");
		// for (int i = 0; i < 10; i++)
		// {
		// 	Serial.print(i);
		// 	Serial.print(": ");

		// 	Serial.print(joinedTargets[i][0]);
		// 	Serial.print(":");
		// 	Serial.print(joinedTargets[i][1]);
		// 	Serial.print(":");
		// 	Serial.print(joinedTargets[i][2]);
		// 	Serial.print(":");
		// 	Serial.print(joinedTargets[i][3]);
		// 	Serial.print(":");
		// 	Serial.print(joinedTargets[i][4]);
		// 	Serial.print(":");
		// 	Serial.println(joinedTargets[i][5]);
		// }
	}
}