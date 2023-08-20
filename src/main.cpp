/**************************************************************************************************
 *
 *      OLED display simple none blocking menu System on esp8266/esp32 - i2c version SSD1306 - 30mar22
 *      
 *      from:  https://github.com/alanesq/BasicOLEDMenu
 *
 *
 **************************************************************************************************

 The sketch displays a menu on the oled and when an item is selected it sets a
 flag and waits until the event is acted upon.  Max menu items on a 128x64 oled
 is four.

 Notes:   text size 1 = 21 x 8 characters on the larger oLED display
          text size 2 = 10 x 4

 For more oled info    see: https://randomnerdtutorials.com/guide-for-oled-display-with-arduino/

 See the "menus below here" section for examples of how to use the menus

 Note: If you get garbage on the display and the device locking up etc. it may just be a poor connection
       to the rotary encoder


 **************************************************************************************************/
const char version[4] = "0.2";

#include <Arduino.h>         // required by platformIO
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "BluetoothA2DPSink.h"
#include <EEPROM.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <AsyncElegantOTA.h>

BluetoothA2DPSink a2dp_sink;

// ----------------------------------------------------------------
//                         S E T T I N G S
// ----------------------------------------------------------------

//  esp32 lolin lite gpio pins
#define encoder0PinA  32                  // Rotary encoder gpio pin - 16
#define encoder0PinB  33                  // Rotary encoder gpio pin - 17
#define encoder0Press 25                  // Rotary encoder button gpio pin - 23
#define OLEDC 22                          // oled clock pin (set to -1 for default) - 26
#define OLEDD 21                          // oled data pin - 27
#define OLEDE -1                          // oled enable pin (set to -1 if not used)

// oLED
#define OLED_ADDR 0x3C                    // OLED i2c address
#define SCREEN_WIDTH 128                  // OLED display width, in pixels (usually 128)
#define SCREEN_HEIGHT 64                  // OLED display height, in pixels (64 for larger oLEDs)
#define OLED_RESET -1                     // Reset pin gpio (-1 if sharing Arduino reset pin)

// Misc
const int serialDebug = 1;
const int iLED = 22;						// onboard indicator led gpio pin
#define BUTTONPRESSEDSTATE 0				// rotary encoder gpio pin logic level when the button is pressed (usually 0)
#define DEBOUNCEDELAY 100					// debounce delay for button inputs
const int menuTimeout = 10;					// menu inactivity timeout (seconds)
const bool menuLargeText = 0;				// show larger text when possible (if struggling to read the small text)
const int maxmenuItems = 12;				// max number of items used in any of the menus (keep as low as possible to save memory)
const int itemTrigger = 1;					// rotary encoder - counts per tick (varies between encoders usually 1 or 2)
const int topLine = 18;						// y position of lower area of the display (18 with two colour displays)
const byte lineSpace1 = 9;					// line spacing for textsize 1 (small text)
const byte lineSpace2 = 17;					// line spacing for textsize 2 (large text)
const int displayMaxLines = 5;				// max lines that can be displayed in lower section of display in textsize1 (5 on larger oLeds)
const int MaxmenuTitleLength = 10;			// max characters per line when using text size 2 (usually 10)

byte volume = 0;
byte volumeAddr = 0;

const char *ssid     = "BZ_IOT";
const char *password = "Password";

AsyncWebServer server(80);

// -------------------------------------------------------------------------------------------------


// forward declarations
  void doEncoder();
  void controlMenu();
  void menuActions();
  void volumeControl();
  void menuVolume();
  void reUpdateButton();
  void serviceMenu();
  int serviceValue(bool _blocking);
  void displayMessage(String _title, String _message);
  void resetMenu();


  enum menuModes {
      off,                                  // display is off
      menu,                                 // a menu is active
      value,                                // 'enter a value' none blocking is active
      message,                              // displaying a message
      blocking                              // a blocking procedure is in progress (see enter value)
  };
  menuModes menuMode = off;                 // default mode at startup is off

  struct oledMenus {
    // menu
    String menuTitle = "";                    // the title of active mode
    int noOfmenuItems = 0;                    // number if menu items in the active menu
    int selectedMenuItem = 0;                 // when a menu item is selected it is flagged here until actioned and cleared
    int highlightedMenuItem = 0;              // which item is curently highlighted in the menu
    String menuItems[maxmenuItems+1];         // store for the menu item titles
    uint32_t lastMenuActivity = 0;            // time the menu last saw any activity (used for timeout)
    // 'enter a value'
    int mValueEntered = 0;                    // store for number entered by value entry menu
    int mValueLow = 0;                        // lowest allowed value
    int mValueHigh = 0;                       // highest allowed value
    int mValueStep = 0;                       // step size when encoder is turned
  };
  oledMenus oledMenu;

  struct rotaryEncoders {
    volatile int encoder0Pos = 0;             // current value selected with rotary encoder (updated by interrupt routine)
    volatile bool encoderPrevA;               // used to debounced rotary encoder
    volatile bool encoderPrevB;               // used to debounced rotary encoder
    uint32_t reLastButtonChange = 0;          // last time state of button changed (for debouncing)
    bool encoderPrevButton = 0;               // used to debounce button
    int reButtonDebounced = 0;                // debounced current button state (1 when pressed)
    const bool reButtonPressedState = BUTTONPRESSEDSTATE;  // the logic level when the button is pressed
    const uint32_t reDebounceDelay = DEBOUNCEDELAY;        // button debounce delay setting
    bool reButtonPressed = 0;                 // flag set when the button is pressed (it has to be manually reset)
  };
  rotaryEncoders rotaryEncoder;

// oled SSD1306 display connected to I2C
  Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);


// -------------------------------------------------------------------------------------------------
//                                 The custom menus go below here
// -------------------------------------------------------------------------------------------------


// Start the default menu
void defaultMenu() {
  controlMenu();
}

void controlMenu() {
  resetMenu();								// clear any previous menu
  menuMode = menu;							// enable menu mode
  oledMenu.noOfmenuItems = 5;				// set the number of items in this menu
  oledMenu.menuTitle	= "Control Menu";	// menus title (used to identify it)
  oledMenu.menuItems[1] = "Exit";
  oledMenu.menuItems[2] = "Pause";			// set the menu items
  oledMenu.menuItems[3] = "Play";
  oledMenu.menuItems[4] = "Volume";
  oledMenu.menuItems[5] = "IP Address";
}

void menuActions() {
  if (oledMenu.menuTitle == "Control Menu") {
    if (oledMenu.selectedMenuItem == 1) {
      resetMenu();
    }
    if (oledMenu.selectedMenuItem == 2) {
      resetMenu();
      a2dp_sink.pause();
      defaultMenu();
    }
    if (oledMenu.selectedMenuItem == 3) {
      resetMenu();
      a2dp_sink.play();
      defaultMenu();
    }
    if (oledMenu.selectedMenuItem == 4) {
      resetMenu();
      volumeControl();
    }
    if (oledMenu.selectedMenuItem == 5) {
      resetMenu();
      displayMessage("IP Address", WiFi.localIP().toString());
    }
    oledMenu.selectedMenuItem = 0;
  }

}  // menuActions

//                -----------------------------------------------

void volumeControl() {
	resetMenu();							          // clear any previous menu
	menuMode = value;						        // enable value entry
	oledMenu.menuTitle = "Volume";		  // title (used to identify which number was entered)
	oledMenu.mValueLow = 0;				      // minimum value allowed
	oledMenu.mValueHigh = 100;			    // maximum value allowed
	oledMenu.mValueStep = 1;				    // step size
	oledMenu.mValueEntered = volume;		// starting value
}

void menuVolume() {
	if (oledMenu.menuTitle == "Volume") {
		volume = oledMenu.mValueEntered;
		EEPROM.put(volumeAddr, volume);
		EEPROM.commit();
    a2dp_sink.set_volume(volume);
		String tString = String(volume);
		displayMessage("Entered", "\n\nVolume : " + tString);
	}
}


// -------------------------------------------------------------------------------------------------
//                                         custom menus go above here
// -------------------------------------------------------------------------------------------------


// ----------------------------------------------------------------
//                   -button debounce (rotary encoder)
// ----------------------------------------------------------------
// update rotary encoder current button status

void reUpdateButton() {
    bool tReading = digitalRead(encoder0Press);        // read current button state
    if (tReading != rotaryEncoder.encoderPrevButton) rotaryEncoder.reLastButtonChange = millis();     // if it has changed reset timer
    if ( (unsigned long)(millis() - rotaryEncoder.reLastButtonChange) > rotaryEncoder.reDebounceDelay ) {  // if button state is stable
      if (rotaryEncoder.encoderPrevButton == rotaryEncoder.reButtonPressedState) {
        if (rotaryEncoder.reButtonDebounced == 0) {    // if the button has been pressed
          rotaryEncoder.reButtonPressed = 1;           // flag set when the button has been pressed
          if (menuMode == off) defaultMenu();          // if the display is off start the default menu
        }
        rotaryEncoder.reButtonDebounced = 1;           // debounced button status  (1 when pressed)
      } else {
        rotaryEncoder.reButtonDebounced = 0;
      }
    }
    rotaryEncoder.encoderPrevButton = tReading;            // update last state read
}


// ----------------------------------------------------------------
//                    -update the active menu
// ----------------------------------------------------------------

void menuUpdate() {

  if (menuMode == off) return;    // if menu system is turned off do nothing more

  // if no recent activity then turn oled off
    if ( (unsigned long)(millis() - oledMenu.lastMenuActivity) > (menuTimeout * 1000) ) {
      resetMenu();
      return;
    }

    switch (menuMode) {

      // if there is an active menu
      case menu:
        serviceMenu();
        menuActions();
        break;

      // if there is an active none blocking 'enter value'
      case value:
        serviceValue(0);
        if (rotaryEncoder.reButtonPressed) {                        // if the button has been pressed
          menuVolume();                                             // a value has been entered so action it
          break;
        }

      // if a message is being displayed
      case message:
        if (rotaryEncoder.reButtonPressed == 1) defaultMenu();    // if button has been pressed return to default menu
        break;
    }
}


// ----------------------------------------------------------------
//                       -service active menu
// ----------------------------------------------------------------

void serviceMenu() {

    // rotary encoder
      if (rotaryEncoder.encoder0Pos >= itemTrigger) {
        rotaryEncoder.encoder0Pos -= itemTrigger;
        oledMenu.highlightedMenuItem++;
        oledMenu.lastMenuActivity = millis();   // log time
      }
      if (rotaryEncoder.encoder0Pos <= -itemTrigger) {
        rotaryEncoder.encoder0Pos += itemTrigger;
        oledMenu.highlightedMenuItem--;
        oledMenu.lastMenuActivity = millis();   // log time
      }
      if (rotaryEncoder.reButtonPressed == 1) {
        oledMenu.selectedMenuItem = oledMenu.highlightedMenuItem;     // flag that the item has been selected
        oledMenu.lastMenuActivity = millis();   // log time
        if (serialDebug) Serial.println("menu '" + oledMenu.menuTitle + "' item '" + oledMenu.menuItems[oledMenu.highlightedMenuItem] + "' selected");
      }

    const int _centreLine = displayMaxLines / 2 + 1;    // mid list point
    display.clearDisplay();
    display.setTextColor(WHITE);

    // verify valid highlighted item
      if (oledMenu.highlightedMenuItem > oledMenu.noOfmenuItems) oledMenu.highlightedMenuItem = oledMenu.noOfmenuItems;
      if (oledMenu.highlightedMenuItem < 1) oledMenu.highlightedMenuItem = 1;

    // title
      display.setCursor(0, 0);
      if (menuLargeText) {
        display.setTextSize(2);
        display.println(oledMenu.menuItems[oledMenu.highlightedMenuItem].substring(0, MaxmenuTitleLength));
      } else {
        if (oledMenu.menuTitle.length() > MaxmenuTitleLength) display.setTextSize(1);
        else display.setTextSize(2);
        display.println(oledMenu.menuTitle);
      }
      display.drawLine(0, topLine-1, display.width(), topLine-1, WHITE);       // draw horizontal line under title

    // menu
      display.setTextSize(1);
      display.setCursor(0, topLine);
      for (int i=1; i <= displayMaxLines; i++) {
        int item = oledMenu.highlightedMenuItem - _centreLine + i;
        if (item == oledMenu.highlightedMenuItem) display.setTextColor(BLACK, WHITE);
        else display.setTextColor(WHITE);
        if (item > 0 && item <= oledMenu.noOfmenuItems) display.println(oledMenu.menuItems[item]);
        else display.println(" ");
      }

    //// how to display some updating info. on the menu screen
    // display.setCursor(80, 25);
    // display.println(millis());
 
    display.display();
}


// ----------------------------------------------------------------
//                        -service value entry
// ----------------------------------------------------------------
// if _blocking set to 1 then all other tasks are stopped until a value is entered

int serviceValue(bool _blocking) {

  const int _valueSpacingX = 30;      // spacing for the displayed value y position
  const int _valueSpacingY = 5;       // spacing for the displayed value y position

  if (_blocking) {
    menuMode = blocking;
    oledMenu.lastMenuActivity = millis();   // log time of last activity (for timeout)
  }
  uint32_t tTime;

  do {
    // rotary encoder
      if (rotaryEncoder.encoder0Pos >= itemTrigger) {
        rotaryEncoder.encoder0Pos -= itemTrigger;
        oledMenu.mValueEntered-= oledMenu.mValueStep;
        oledMenu.lastMenuActivity = millis();   // log time
      }
      if (rotaryEncoder.encoder0Pos <= -itemTrigger) {
        rotaryEncoder.encoder0Pos += itemTrigger;
        oledMenu.mValueEntered+= oledMenu.mValueStep;
        oledMenu.lastMenuActivity = millis();   // log time
      }
      if (oledMenu.mValueEntered < oledMenu.mValueLow) {
        oledMenu.mValueEntered = oledMenu.mValueLow;
        oledMenu.lastMenuActivity = millis();   // log time
      }
      if (oledMenu.mValueEntered > oledMenu.mValueHigh) {
        oledMenu.mValueEntered = oledMenu.mValueHigh;
        oledMenu.lastMenuActivity = millis();   // log time
      }

      display.clearDisplay();
      display.setTextColor(WHITE);

      // title
        display.setCursor(0, 0);
        if (oledMenu.menuTitle.length() > MaxmenuTitleLength) display.setTextSize(1);
        else display.setTextSize(2);
        display.println(oledMenu.menuTitle);
        display.drawLine(0, topLine-1, display.width(), topLine-1, WHITE);       // draw horizontal line under title

      // value selected
        display.setCursor(_valueSpacingX, topLine + _valueSpacingY);
        display.setTextSize(3);
        display.println(oledMenu.mValueEntered);

      // range
        display.setCursor(0, display.height() - lineSpace1 - 1 );   // bottom of display
        display.setTextSize(1);
        display.println(String(oledMenu.mValueLow) + " to " + String(oledMenu.mValueHigh));

      // bar
        int Tlinelength = map(oledMenu.mValueEntered, oledMenu.mValueLow, oledMenu.mValueHigh, 0 , display.width());
        display.drawLine(0, display.height()-1, Tlinelength, display.height()-1, WHITE);

      display.display();

      reUpdateButton();        // check status of button
      tTime = (unsigned long)(millis() - oledMenu.lastMenuActivity);      // time since last activity

  } while (_blocking && rotaryEncoder.reButtonPressed == 0 && tTime < (menuTimeout * 1000));        // if in blocking mode repeat until button is pressed or timeout

  if (_blocking) menuMode = off;

  return oledMenu.mValueEntered;        // used when in blocking mode

}

// ----------------------------------------------------------------
//                         -message display
// ----------------------------------------------------------------
// 21 characters per line, use "\n" for next line
// assistant:  <     line 1        ><     line 2        ><     line 3        ><     line 4         >

 void displayMessage(String _title, String _message) {
  resetMenu();
  menuMode = message;

  display.clearDisplay();
  display.setTextColor(WHITE);

  // title
    display.setCursor(0, 0);
    if (menuLargeText) {
      display.setTextSize(2);
      display.println(_title.substring(0, MaxmenuTitleLength));
    } else {
      if (_title.length() > MaxmenuTitleLength) display.setTextSize(1);
      else display.setTextSize(2);
      display.println(_title);
    }

  // message
    display.setCursor(0, topLine);
    display.setTextSize(1);
    display.println(_message);

  display.display();

 }


// ----------------------------------------------------------------
//                        -reset menu system
// ----------------------------------------------------------------

void resetMenu() {
    menuMode = off;
    oledMenu.selectedMenuItem = 0;
    rotaryEncoder.encoder0Pos = 0;
    oledMenu.noOfmenuItems = 0;
    oledMenu.menuTitle = "";
    oledMenu.highlightedMenuItem = 0;
    oledMenu.mValueEntered = 0;
    rotaryEncoder.reButtonPressed = 0;

  oledMenu.lastMenuActivity = millis();   // log time

  // clear oled display
    display.clearDisplay();
    display.display();
}


// ----------------------------------------------------------------
//                     -interrupt for rotary encoder
// ----------------------------------------------------------------
// rotary encoder interrupt routine to update position counter when turned
//     interrupt info: https://www.gammon.com.au/forum/bbshowpost.php?id=11488

void doEncoder() {

  bool pinA = digitalRead(encoder0PinA);
  bool pinB = digitalRead(encoder0PinB);

  if ( (rotaryEncoder.encoderPrevA == pinA && rotaryEncoder.encoderPrevB == pinB) ) return;  // no change since last time (i.e. reject bounce)

  // same direction (alternating between 0,1 and 1,0 in one direction or 1,1 and 0,0 in the other direction)
         if (rotaryEncoder.encoderPrevA == 1 && rotaryEncoder.encoderPrevB == 0 && pinA == 0 && pinB == 1) rotaryEncoder.encoder0Pos -= 1;
    else if (rotaryEncoder.encoderPrevA == 0 && rotaryEncoder.encoderPrevB == 1 && pinA == 1 && pinB == 0) rotaryEncoder.encoder0Pos -= 1;
    else if (rotaryEncoder.encoderPrevA == 0 && rotaryEncoder.encoderPrevB == 0 && pinA == 1 && pinB == 1) rotaryEncoder.encoder0Pos += 1;
    else if (rotaryEncoder.encoderPrevA == 1 && rotaryEncoder.encoderPrevB == 1 && pinA == 0 && pinB == 0) rotaryEncoder.encoder0Pos += 1;

  // change of direction
    else if (rotaryEncoder.encoderPrevA == 1 && rotaryEncoder.encoderPrevB == 0 && pinA == 0 && pinB == 0) rotaryEncoder.encoder0Pos += 1;
    else if (rotaryEncoder.encoderPrevA == 0 && rotaryEncoder.encoderPrevB == 1 && pinA == 1 && pinB == 1) rotaryEncoder.encoder0Pos += 1;
    else if (rotaryEncoder.encoderPrevA == 0 && rotaryEncoder.encoderPrevB == 0 && pinA == 1 && pinB == 0) rotaryEncoder.encoder0Pos -= 1;
    else if (rotaryEncoder.encoderPrevA == 1 && rotaryEncoder.encoderPrevB == 1 && pinA == 0 && pinB == 1) rotaryEncoder.encoder0Pos -= 1;

  //else if (serialDebug) Serial.println("Error: invalid rotary encoder pin state - prev=" + String(rotaryEncoder.encoderPrevA) + ","
  //                                      + String(rotaryEncoder.encoderPrevB) + " new=" + String(pinA) + "," + String(pinB));

  // update previous readings
    rotaryEncoder.encoderPrevA = pinA;
    rotaryEncoder.encoderPrevB = pinB;
}

void connectToWifi() {
	Serial.println("Connecting to Wi-Fi...");
	WiFi.begin(ssid, password);
	WiFi.setAutoReconnect(true);
	WiFi.persistent(true);
	Serial.print("Connected to ");
	Serial.println(WiFi.SSID());
}

// ----------------------------------------------------------------
//                              -setup
// ----------------------------------------------------------------
// called from main setup

void setup() {

  Serial.begin(115200); while (!Serial); delay(50);       // start serial comms
  Serial.println("\n\n\nStarting menu demo\n");
	EEPROM.begin(1);
	EEPROM.get(volumeAddr, volume);
  connectToWifi();
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    	request->send(200, "text/plain", "Hi! I am ESP32. ESP32-Music\nVersion: " + String(version));
	});

	AsyncElegantOTA.begin(&server);    // Start ElegantOTA
	server.begin();
	Serial.println("HTTP server started");
	Serial.print("OK");
  i2s_pin_config_t pin_config = {
        .bck_io_num = 4,
        .ws_io_num = 15,
        .data_out_num = 2,
        .data_in_num = I2S_PIN_NO_CHANGE
    };
    a2dp_sink.set_pin_config(pin_config);
    a2dp_sink.start("ESP-Music");
    a2dp_sink.set_volume(volume);

  pinMode(iLED, OUTPUT);     // onboard indicator led

  // configure gpio pins for rotary encoder
    pinMode(encoder0Press, INPUT_PULLUP);
    pinMode(encoder0PinA, INPUT);
    pinMode(encoder0PinB, INPUT);

      if (OLEDE != 0) {
        pinMode(OLEDE , OUTPUT);
        digitalWrite(OLEDE, HIGH);
      }
    if (0 == OLEDC) Wire.begin();
    else Wire.begin(OLEDD, OLEDC);
    if(!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
      if (serialDebug) Serial.println(("\nError initialising the oled display"));
    }

  // Interrupt for reading the rotary encoder position
    rotaryEncoder.encoder0Pos = 0;
    attachInterrupt(digitalPinToInterrupt(encoder0PinA), doEncoder, CHANGE);

  //defaultMenu();       // start the default menu

  // display greeting message - pressing button will start menu
    displayMessage("Welcome", "Bluetooth name\n   ESP-Music\nV" + String(version));

}


// ----------------------------------------------------------------
//                              -loop
// ----------------------------------------------------------------
// called from main loop

void loop() {

  reUpdateButton();      // update rotary encoder button status (if pressed activate default menu)
  menuUpdate();          // update or action the oled menu

 

  // flash onboard led
    static uint32_t ledTimer = millis();
    if ( (unsigned long)(millis() - ledTimer) > 1000 ) {
      digitalWrite(iLED, !digitalRead(iLED));
      ledTimer = millis();
    }

}  // oledLoop