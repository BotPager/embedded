Overview

BotPager/embedded is a customized fork of Meshtastic firmware optimized for pager-style message reception and notification. The implementation adds intelligent message routing, LED control, and hardware management specifically designed for the T-LoRa Pager device.

TextMessageModule Customizations

Message Format Support
The TextMessageModule handles two message formats:
- Old Format: ID|Message
Routes message based on device ID
LED defaults to red (0xFF0000)

- New Format: ID|RRGGBB|Message
Routes message based on device ID
Extracts RGB hex color for LED (e.g., FF00FF for magenta)
Displays only the message body on screen

Message Processing Pipeline
- Parse for Pager ID: Incoming mesh packets are checked against the device's configured PAGER_ID
- Extract Color: If a valid 6-character hex color is present, it's extracted and converted to RGB
- Store Message: The message body (without ID/color) is stored in device state
- Activate Hardware: GPIO pins are enabled for converter, buzzer, and LED drivers
- Set LED Color: NeoPixel array (20 LEDs) is set to the parsed RGB value
- Trigger UI: Screen is awakened and forced to redraw
- Notify System: Observers are notified of the incoming message

Key Functions
- parseForPagerID(): Parses raw message bytes to extract ID, color, and body
- handleReceived(): Main message handler that orchestrates hardware activation
- getLocalPagerID(): Returns device's pager ID (from PAGER_ID define or device short name)
- normalizeBlockDelimiters(): Handles escaped newlines from JSON/automation tools
- isHexColor(): Validates 6-character hex color strings

Main.cpp Hardware Integration

GPIO Pin Configuration
- Pin 38: Zero Control (OUTPUT, initially LOW)
- Pin 41: Converter Enable (OUTPUT, initially LOW)
- Pin 42: Buzzer FET Enable (OUTPUT, initially LOW)
- Pin 46: LED FET Enable (OUTPUT, initially LOW)

NeoPixel Setup

20 addressable RGB LEDs configured at startup
Brightness set to NEOPIXEL_BRIGHTNESS constant
Initialized to OFF state

LED/Buzzer Timeout Management
In the loop() function:
LED auto-disables after 20 seconds (Defined by NOTIFICATION_TIMEOUT_MS in the configuration.h file if no button press
Button press while LED is on immediately disables hardware
Disables converter, buzzer FET, and LED FET on timeout/button
Clears NeoPixel array when disabling

Screen System Simplification

Text Message Frame Integration
Text message frame is always included in the frame list
Frame defaults to showing received text messages
Frame visibility controlled by devicestate.has_rx_text_message flag
Frame Focus Management
The screen prioritizes message display through setFrames() with focus parameter:
- FOCUS_TEXTMESSAGE: Immediately switches to text message frame
- FOCUS_DEFAULT: Returns to text message after alert dismissal
- FOCUS_PRESERVE: Maintains current frame while updating frame list

Screen Wake Behavior

When a message arrives:
Screen is turned on (if shouldWakeOnReceivedMessage() returns true)
forceDisplay() is called to trigger immediate redraw
Frame list is regenerated with FOCUS_TEXTMESSAGE to show message
Unread message indicator is enabled

Message Display Details

Only messages matching the device's pager ID are displayed
Message body is shown without ID or color prefix
Battery overlay available via holding down button on message frame

Device Identification
The pager identifies itself via:

PAGER_ID macro (if defined at compile time) [This is never really used]
Falls back to device short name from configuration