#include "TextMessageModule.h"
#include "MeshService.h"
#include "NodeDB.h"
#include "PowerFSM.h"
#include "buzz.h"
#include "configuration.h"
#include "graphics/Screen.h"
#include "graphics/NeoPixel.h"
#include <cctype>
#include <sstream>
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
#ifdef PAGER_ID
    return std::to_string(PAGER_ID);
#else
    return std::string(owner.short_name);
#endif
}

// Helper function: Parse hex string to uint32_t
static uint32_t hexToUint32(const std::string &hex){
    try {
        return std::stoul(hex, nullptr, 16);
    } catch (...) {
        return 0;
    }
}

static bool isHexColor(const std::string &s){
    if (s.size() != 6) {
        return false;
    }
    for (char c : s) {
        if (!std::isxdigit(static_cast<unsigned char>(c))) {
            return false;
        }
    }
    return true;
}

// Support payloads that contain escaped newlines ("\\n") from JSON/automation tools.
static std::string normalizeBlockDelimiters(const std::string &msg){
    std::string out;
    out.reserve(msg.size());

    for (size_t i = 0; i < msg.size(); ++i) {
        if (msg[i] == '\\' && (i + 1) < msg.size()) {
            const char n = msg[i + 1];
            if (n == 'n') {
                out.push_back('\n');
                ++i;
                continue;
            }
            if (n == 'r') {
                ++i;
                if ((i + 1) < msg.size() && msg[i + 1] == '\\' && (i + 2) < msg.size() && msg[i + 2] == 'n') {
                    ++i;
                    ++i;
                }
                out.push_back('\n');
                continue;
            }
        }
        out.push_back(msg[i]);
    }

    return out;
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
    msg = normalizeBlockDelimiters(msg);

    // Split the message block by newlines
    std::istringstream iss(msg);
    std::string line;

    while (std::getline(iss, line)) {
        line = trim_message(line);
        if (line.empty()) continue;

        // Find first pipe character to split off the prefix (ID) from the body
        size_t firstPipe = line.find('|');
        if (firstPipe == std::string::npos) continue;

        std::string prefix = trim_message(line.substr(0,firstPipe));

        // Check if this line is intended for this pager
        if (prefix != localId) continue;

       // We found our ID! Now parse the color and message
        std::string remainder = line.substr(firstPipe + 1);
        size_t secondPipe = remainder.find('|');

        uint32_t ledColor = 0xFF0000; // Default to red
        std::string body;

        if (secondPipe == std::string::npos){
            // Old format: ID|Message (no color)
            body = trim_message(remainder);
        } else {
            // New format: ID|RRGGBB|Message
            std::string colorStr = trim_message(remainder.substr(0, secondPipe));
            if (isHexColor(colorStr)){
                ledColor = hexToUint32(colorStr);
                if (ledColor > 0xFFFFFF){ // Ensure it's a valid RGB value
                    ledColor = 0xFF0000; // Default to red if invalid
                }
                body = trim_message(remainder.substr(secondPipe + 1));
            } else {
                // Not a valid color token; treat everything after ID as body.
                body = trim_message(remainder);
            }
        }

        // Found our message and parsed it successfully
        return {true, body, ledColor};
    }

    // ID not found in the block
    return {false, "", 0xFF0000};
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
