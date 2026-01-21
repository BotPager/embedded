#include "TextMessageModule.h"
#include "MeshService.h"
#include "NodeDB.h"
#include "PowerFSM.h"
#include "buzz.h"
#include "configuration.h"
#include "graphics/Screen.h"
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
    return "";
    #endif
}

// ID Parser. Accepts messages of form "1234|Message"
static std::pair <bool, std::string> parseForPagerID(const uint8_t *bytes, size_t len, const std::string &localId){
    if (len == 0){
        return {false, ""};
    }
    if (localId.empty()){
        return {false, ""};
    }

    std::string msg(reinterpret_cast<const char*>(bytes), len);
    size_t pos = msg.find("|");
    // If it can't find it return false
    if (pos == std::string::npos){
        return {false, ""};
    }

    std::string prefix = trim_message(msg.substr(0,pos));
    std::string body = trim_message(msg.substr(pos+1));

    if (prefix == localId){
        return{true,body};
    }
    return {false,""};
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

    if (!parsed.first){
        // Ignore message
        return ProcessMessage::CONTINUE;
    }
    // We only store/display messages destined for us.
    // Keep a copy of the most recent text message.
    devicestate.rx_text_message = mp;
    devicestate.has_rx_text_message = true;


    // Turn on GPIO Pin
    digitalWrite(38, HIGH);
    ledOnTime = millis();
    isLedOn = true;


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
