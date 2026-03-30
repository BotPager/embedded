#include "TextMessageModule.h"
#include "MeshService.h"
#include "NodeDB.h"
#include "PowerFSM.h"
#include "buzz.h"
#include "configuration.h"
#include "graphics/Screen.h"
#include "graphics/NeoPixel.h"
TextMessageModule *textMessageModule;
extern bool isLedOn;
extern unsigned long ledOnTime;

// Remove and leading or trailing white space
static inline std::string trim_message(const std::string &s){
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

// Helper function: return PAGER_ID if it's defined
static std::string getLocalPagerID(){
    return std::string(owner.short_name);
}

// Helper function: Parse hex string to uint32_t
static uint32_t hexToUint32(const std::string &hex){
    try {
        return std::stoul(hex, nullptr, 16);
    } catch (...) {
        return 0;
    }
}

// ID Parser. Accepts messages of form:
// "1234|Message" (old format, LED will be red)
// "1234|FF0000|Message" (new format with RGB color)
struct ParsedMessage {
    bool isValid;
    std::string body;
    uint32_t ledRgb;
};


// ID Parser. Accepts messages of form "1234|Message"
static ParsedMessage parseForPagerID(const uint8_t *bytes, size_t len, const std::string &localId){
    if (len == 0){
        return {false, "", 0xFF0000}; // Default to red LED
    }
    if (localId.empty()){
        return {false, "", 0xFF0000}; // Default to red LED
    }

    std::string msg(reinterpret_cast<const char*>(bytes), len);

    // Find first pipe character to split off the prefix (ID) from the body
    size_t firstPipe = msg.find("|");
    // If it can't find it return false
    if (firstPipe == std::string::npos){
        return {false, "", 0xFF0000}; // Default to red LED
    }

    std::string prefix = trim_message(msg.substr(0,firstPipe));

    if (prefix != localId){
        return{false,"", 0xFF0000};
    }

    // Find second pipe character to split off optional RGB color
    std::string remainder = msg.substr(firstPipe + 1);
    size_t secondPipe = remainder.find("|");

    uint32_t ledColor = 0xFF0000; // Default to red
    std::string body;

    if (secondPipe == std::string::npos){
        body = trim_message(remainder);
    } else {
        std::string colorStr = trim_message(remainder.substr(0, secondPipe));
        body = trim_message(remainder.substr(secondPipe + 1));

        if (!colorStr.empty() && colorStr.size() <= 6){ // Basic validation for hex color
            ledColor = hexToUint32(colorStr);
            if (ledColor > 0xFFFFFF){ // Ensure it's a valid RGB value
                ledColor = 0xFF0000; // Default to red if invalid
            }
        }
    }
    return {true,body, ledColor};
}



ProcessMessage TextMessageModule::handleReceived(const meshtastic_MeshPacket &mp)
{
#if defined(DEBUG_PORT) && !defined(DEBUG_MUTE)
    auto &p = mp.decoded;
    LOG_INFO("Received text msg from=0x%0x, id=0x%x, msg=%.*s", mp.from, mp.id, p.payload.size, p.payload.bytes);
#endif


    // Decide whether this message is intended for this pager
    auto &payload = mp.decoded.payload;
    std::string localId =  getLocalPagerID();

    auto parsed = parseForPagerID(payload.bytes,payload.size,localId);

    if (!parsed.isValid){
        // Ignore message
        return ProcessMessage::CONTINUE;
    }
    // We only store/display messages destined for us.
    // Keep a copy of the most recent text message.
    devicestate.rx_text_message = mp;
    
    // Replace the payload with the parsed body so only the message (not the ID) is displayed.
    std::string body = parsed.body;
    size_t max_payload = sizeof(devicestate.rx_text_message.decoded.payload.bytes);
    size_t copylen = (body.size() < (max_payload - 1)) ? body.size() : (max_payload - 1);

    // copy the body and ensure null-termination (some renderers use "%s")
    memcpy(devicestate.rx_text_message.decoded.payload.bytes, body.data(), copylen);
    devicestate.rx_text_message.decoded.payload.bytes[copylen] = '\0';
    devicestate.rx_text_message.decoded.payload.size = copylen;
    
    devicestate.has_rx_text_message = true;

    // Turn on GPIO Pin
    digitalWrite(41, HIGH); // Converter Enable
    digitalWrite(42, HIGH); // Buzzer FET Enable
    digitalWrite(46, HIGH); // LED FET Enable

    // Extract LED color from parsed message
    uint8_t r = (parsed.ledRgb >> 16) & 0xFF;
    uint8_t g = (parsed.ledRgb >> 8) & 0xFF;
    uint8_t b = parsed.ledRgb & 0xFF;
    
    // Set the Neopixel to the parsed color
    for (int i = 0; i < 20; i++){
        pixels.setPixelColor(i, pixels.Color(r, g, b));
    }
    pixels.show();

    ledOnTime = millis();
    isLedOn = true;
    // Flash the screen
    screen->blink();



    // Only trigger screen wake if configuration allows it
    if (shouldWakeOnReceivedMessage()) {
        powerFSM.trigger(EVENT_RECEIVED_MSG);
    }
    notifyObservers(&mp);

    return ProcessMessage::CONTINUE; // Let others look at this message also if they want
}

bool TextMessageModule::wantPacket(const meshtastic_MeshPacket *p)
{
    return MeshService::isTextPayload(p);
}
