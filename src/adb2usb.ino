#include <USBComposite.h>
#include <USBHID.h>
//#include "hid_keyboard.h"
#include "adb_structures.h"
#include "adb_devices.h"
#include "keymap.h"


#define POLL_DELAY    5

#define LED PB12  
USBHID HID;
HIDKeyboard Keyboard(HID);
HIDMouse Mouse(HID); 
USBCompositeSerial CompositeSerial;

bool capsLock = false;
bool apple_extended_detected = false;
bool keyboard_present = false, mouse_present = false;

uint8_t lastLEDs = 0xFF;

void setup() {
    // Turn the led on at the beginning of setup
    pinMode(LED, OUTPUT);
    digitalWrite(LED, HIGH);
    
    HID.begin(CompositeSerial, HID_KEYBOARD_MOUSE);
    while(!USBComposite);

    // Set up HID
    //hid_keyboard_init();

    digitalWrite(LED,HIGH);
    // Set up the ADB bus
    adb_init();

    delay(1000); // A wait for good measure, apparently AEKII can take a moment to reset

    // Initialise the ADB devices
    // Switch the keyboard to Apple Extended if available
    bool error;
    adb_data<adb_register3> reg3 = {0}, mask = {0};
    do {
      uint32_t t0 = millis();
      do {
          error = false;
          reg3.data.device_handler_id = 0x03;
          mask.data.device_handler_id = 0xFF;
          apple_extended_detected = adb_device_update_register3(ADB_ADDR_KEYBOARD, reg3, mask.raw, &error);
          delay(20);
      } while(error && millis() < t0+2000);
      if (!error) keyboard_present = true;
  
      t0 = millis();
      // Switch the mouse to higher resolution, if available
      // TODO: Apple Extended Mouse Protocol (Handler = 4)
      error = false;
      do {
        reg3.raw = 0;
        mask.raw = 0;
        reg3.data.device_handler_id = 0x02;
        mask.data.device_handler_id = 0xFF;
        adb_device_update_register3(ADB_ADDR_MOUSE, reg3, mask.raw, &error);
        delay(20);
      } while(error && millis() < t0+2000);
      if (!error) mouse_present = true;
    } while(!keyboard_present && !mouse_present);

    if (keyboard_present) {
      Keyboard.begin();
      Keyboard.setAdjustForHostCapsLock(false);
    }

    // Set-up successful, turn of the LED
    digitalWrite(LED, LOW);
}

void handleKey(uint8_t key, bool released) {
    if (key != ADB_KEY_CAPS_LOCK) {
      uint16_t k = adb_keycode_to_arduino_hid[key];
      if (k) {
        if (released)
          Keyboard.release(k);
        else
          Keyboard.press(k);
      }
    }
    else {
      if (capsLock == released) {
        Keyboard.press(KEY_CAPS_LOCK);
        delay(80);
        Keyboard.release(KEY_CAPS_LOCK);
        capsLock = !released;        
      }
    }
}

void keyboard_handler() {
    bool error = false;

    auto key_press = adb_keyboard_read_key_press(&error);

#if 0
    if (key_press.raw==0)return;
    char s[256];
    sprintf(s,"%x:%02x %x:%02x", key_press.data.released0, key_press.data.key0, key_press.data.released1, key_press.data.key1);
    Keyboard.println(s);
    return;
#endif    

    if (error) return;  // don't continue changing the hid report if there was
                        // an error reading from ADB – most often it's a timeout

    CompositeSerial.println(key_press.raw,HEX);

    if (key_press.raw == ADB_KEY_POWER_DOWN) {
      Keyboard.press(KEY_MUTE);
    }
    else if (key_press.raw == ADB_KEY_POWER_UP) {
      Keyboard.release(KEY_MUTE);
    }
    else {
      handleKey(key_press.data.key0, key_press.data.released0);
      if (key_press.data.key1 != 0x7F)
        handleKey(key_press.data.key1, key_press.data.released1);
    }
}

void mouse_handler() {
    bool error = false;
    auto mouse_data = adb_mouse_read_data(&error);

    if (error || mouse_data.raw == 0) return;

    int8_t mouse_x = ADB_MOUSE_CONV_AXIS(mouse_data.data.x_offset);
    int8_t mouse_y = ADB_MOUSE_CONV_AXIS(mouse_data.data.y_offset);
    bool button = 0 == mouse_data.data.button;

    if (button) {
      if (!Mouse.isPressed())
        Mouse.press(); 
    }
    else {
      if (Mouse.isPressed())
        Mouse.release();
    }

    Mouse.move(mouse_x, mouse_y);
}

void led_handler() {
    uint8_t leds = Keyboard.getLEDs();

    if (leds != lastLEDs) {
      adb_keyboard_write_leds((leds & 4) != 0, (leds & 2) != 0, leds & 1);
      lastLEDs = leds;
    }  
}

void loop() {
    if (keyboard_present) {
        keyboard_handler();
        led_handler();
        // Wait a tiny bit before polling again,
        // while ADB seems fairly tolerent of quick requests
        // we don't want to overwhelm USB either
        delay(POLL_DELAY);
    }

    if (mouse_present) {
        mouse_handler();
        delay(POLL_DELAY);
    }
}

