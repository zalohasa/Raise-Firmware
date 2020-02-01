#pragma once

#include <Kaleidoscope.h>
#include <Kaleidoscope-LEDControl.h>
#include <Kaleidoscope-Ranges.h>



#define LM_RECORD Key(kaleidoscope::ranges::KALEIDOSCOPE_SAFE_START)
#define LM_DELETE Key(kaleidoscope::ranges::KALEIDOSCOPE_SAFE_START + 1)
#define LM_M(n) Key(kaleidoscope::ranges::KALEIDOSCOPE_SAFE_START + 2 + n)

#define NUM_KEYS (20)
#define MAX_EVENTS_IN_MACRO 14 //This number is the number of events (a key press is one event, a key release is one event). Total keys is this number/2
#define TOTAL_MACROS 8
#define LAST_EEPROM_MACRO_KEY 5
#define TOTAL_PLUGIN_KEYS 9 //Total number of keys this plugins manages. (physical keys)
#define KEY_START_INDEX 8 //Index in the physical keys array of the start (record) key (must come after all the macro keys)

static_assert (LAST_EEPROM_MACRO_KEY < TOTAL_MACROS, "Invalid number of last eeprom key");

namespace Dygma{
namespace plugin{

enum LM_Keys : uint16_t
{
    LM_START_KEYS = kaleidoscope::ranges::KALEIDOSCOPE_SAFE_START,
    LM_DELETE_KEY = kaleidoscope::ranges::KALEIDOSCOPE_SAFE_START + 1,
    LM_SLOT_0_KEY = kaleidoscope::ranges::KALEIDOSCOPE_SAFE_START + 2,
    LM_END_KEYS = kaleidoscope::ranges::KALEIDOSCOPE_SAFE_START + 2 + TOTAL_MACROS
};

class LiveMacrosPlugin : public kaleidoscope::Plugin
{
public:
    enum class state_t
    {
        IDLE,
        RECORDING,
        MAX_KEYS_REACHED,
        ARE_YOU_SURE_TO_OVERWRITE,
        PLAYING
    };

    LiveMacrosPlugin();
    kaleidoscope::EventHandlerResult onSetup();
    kaleidoscope::EventHandlerResult onLayerChange();
    kaleidoscope::EventHandlerResult beforeReportingState();

    kaleidoscope::EventHandlerResult onKeyswitchEvent(Key &mappedKey, KeyAddr key_addr, uint8_t keyState);
    // kaleidoscope::EventHandlerResult onFocusEvent(const char *command);
private:
    state_t current_state_;
    KeyAddr keys_addrs_[TOTAL_PLUGIN_KEYS];
    uint8_t* current_buff_;
    uint8_t* keys_[TOTAL_MACROS];
    uint8_t current_buff_pos_;
    uint8_t macro_to_overwrite_;
    bool initialized_keys_;

};

}
}

extern Dygma::plugin::LiveMacrosPlugin LiveMacros;