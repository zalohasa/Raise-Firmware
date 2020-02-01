#include "LiveMacros.h"

namespace Dygma{
namespace plugin{

using namespace kaleidoscope;

#define LM_KEY_PRESSED B10000000
#define LM_KEY_PRESSED_MASK B01111111


#ifdef __arm__
// should use uinstd.h to define sbrk but Due causes a conflict
extern "C" char* sbrk(int incr);
#else  // __ARM__
extern char *__brkval;
#endif  // __arm__

static int freeMemory() {
  char top;
#ifdef __arm__
  return &top - reinterpret_cast<char*>(sbrk(0));
#elif defined(CORE_TEENSY) || (ARDUINO > 103 && ARDUINO != 151)
  return &top - __brkval;
#else  // __arm__
  return __brkval ? &top - __brkval : &top - __malloc_heap_start;
#endif  // __arm__
}


template <uint T>
static cRGB& blinkLed(cRGB& colorOn, cRGB& colorOff)
{
    static bool blinkState = false;
    static uint16_t blinkTime = 0;

    if (Runtime.hasTimeExpired(blinkTime, T))
    {
        blinkTime = Runtime.millisAtCycleStart();
        blinkState = ! blinkState;
    }
    if(blinkState)
    {
        return colorOn;
    }
    else
    {
        return colorOff;
    }
}

static void playMacroKeyswitchEvent(Key key, uint8_t keyswitch_state) {
  handleKeyswitchEvent(key, UnknownKeyswitchLocation, keyswitch_state | INJECTED);

  kaleidoscope::Runtime.hid().keyboard().sendReport();
  kaleidoscope::Runtime.hid().mouse().sendReport();
}

LiveMacrosPlugin::LiveMacrosPlugin() : 
current_state_(state_t::IDLE), 
current_buff_(nullptr), 
current_buff_pos_(0),
initialized_keys_(false)
{
    memset(keys_, 0, TOTAL_MACROS);
    for (uint8_t i = 0; i < TOTAL_PLUGIN_KEYS; ++i)
    {
        keys_addrs_[i] = KeyAddr::invalid_state;
    }
}

EventHandlerResult LiveMacrosPlugin::onSetup()
{
    return EventHandlerResult::OK;
}
    
EventHandlerResult LiveMacrosPlugin::onLayerChange()
{
    keys_addrs_[KEY_START_INDEX] = UnknownKeyswitchLocation;

    for (auto key_addr: KeyAddr::all()) {
        Key k = Layer.lookupOnActiveLayer(key_addr);
        if (k.getRaw() == LM_START_KEYS) {
            keys_addrs_[KEY_START_INDEX] = key_addr;
            //If this is not the first time we found the start key, don't keep looking for the rest of the keys
            if (initialized_keys_)
                break;
            initialized_keys_ = true;
        }
        else if (k.getRaw() >= LM_SLOT_0_KEY && k.getRaw() <= LM_END_KEYS)
        {
            uint8_t macroNumber = k.getRaw() - LM_SLOT_0_KEY;
            keys_addrs_[macroNumber] = key_addr;
        }
    }

    return EventHandlerResult::OK;
}

EventHandlerResult LiveMacrosPlugin::beforeReportingState()
{
    switch(current_state_)
    {
        case state_t::IDLE:
            if (keys_addrs_[KEY_START_INDEX].isValid())
            {
                cRGB color = breath_compute(170);
                ::LEDControl.setCrgbAt(keys_addrs_[KEY_START_INDEX], color);

                for (uint8_t i = 0; i < TOTAL_MACROS; ++i)
                {
                    cRGB color = {0, 0, 0};
                    if (keys_addrs_[i].isValid())
                    {
                        if (keys_[i])
                        {
                            color.r = 149;
                            color.g = 255;
                            color.b = 0;
                        }
                        ::LEDControl.setCrgbAt(keys_addrs_[i], color);
                    }
                }
            }
            else
            {
                for (uint8_t i = 0; i < TOTAL_PLUGIN_KEYS; ++i)
                {
                    if (keys_addrs_[i].isValid())
                    {
                        ::LEDControl.refreshAt(keys_addrs_[i]);
                    }
                }
            }
        break;
        case state_t::RECORDING:
        case state_t::MAX_KEYS_REACHED:
        case state_t::ARE_YOU_SURE_TO_OVERWRITE:
            if (keys_addrs_[KEY_START_INDEX].isValid())
            {
                cRGB color = {255, 0, 0};
                cRGB off = {0, 0, 0};
                ::LEDControl.setCrgbAt(keys_addrs_[KEY_START_INDEX], blinkLed<100>(color, off));

                for (uint8_t i = 0; i < TOTAL_MACROS; ++i)
                {
                    color = {149, 255, 0};
                    if (keys_addrs_[i].isValid())
                    {
                        if (keys_[i])
                        {
                            color.r = 255;
                            color.g = 0;
                            color.b = 0;
                        }
                        
                        if (current_state_ == state_t::ARE_YOU_SURE_TO_OVERWRITE && macro_to_overwrite_ == i)
                        {
                            cRGB yellow = {209, 220, 27};
                            ::LEDControl.setCrgbAt(keys_addrs_[i], blinkLed<500>(yellow, off));
                        }
                        else
                        {
                            ::LEDControl.setCrgbAt(keys_addrs_[i], color);
                        }
                        
                    }
                }
            }
            else
            {
                for (uint8_t i = 0; i < TOTAL_PLUGIN_KEYS; ++i)
                {
                    if (keys_addrs_[i].isValid())
                    {
                        ::LEDControl.refreshAt(keys_addrs_[i]);
                    }
                }
            }
        break;
    }

}

EventHandlerResult LiveMacrosPlugin::onKeyswitchEvent(Key &mappedKey, KeyAddr key_addr, uint8_t keyState)
{
    
    switch(current_state_)
    {
        case state_t::IDLE:
            if (mappedKey.getRaw() == LM_START_KEYS && keyToggledOn(keyState))
            {
                //Change status to RECORDING
                current_state_ = state_t::RECORDING;
                if (current_buff_)
                {
                    //should never happend
                    free(current_buff_);
                }
                //Allocate memory for the macro keys buffer. 
                current_buff_ = (uint8_t*)malloc((MAX_EVENTS_IN_MACRO * 2) + 1);
                //Set first element of the buffer to 0, as this is the number of events in the macro.
                *current_buff_ = 0;
                current_buff_pos_ = 1;
                return EventHandlerResult::EVENT_CONSUMED;
            } 
            else if (mappedKey.getRaw() >= LM_SLOT_0_KEY && mappedKey.getRaw() <= LM_END_KEYS && keyToggledOn(keyState))
            {
                //Play a saved macro
                uint8_t macroNumber = mappedKey.getRaw() - LM_SLOT_0_KEY;
                if (keys_[macroNumber] != 0)
                {
                    uint8_t* macro = keys_[macroNumber];
                    uint8_t pos = 1;
                    for (uint8_t i = 0; i < macro[0]; i++)
                    {
                        uint8_t flags = macro[pos++];
                        //Check our custom key mask for pressed or released key
                        Key key(macro[pos++], (flags & LM_KEY_PRESSED_MASK));
                        if (flags & LM_KEY_PRESSED)
                        {
                            playMacroKeyswitchEvent(key, IS_PRESSED);
                        }
                        else
                        {
                            playMacroKeyswitchEvent(key, WAS_PRESSED);
                        }
                    }
                }
                return EventHandlerResult::EVENT_CONSUMED;
            }
            else
            {
                return EventHandlerResult::OK;
            }
            
        break;
        case state_t::RECORDING:
            if (mappedKey.getRaw() == LM_START_KEYS && keyToggledOn(keyState))
            {
                //DISCARD CURRENT RECORDING
                current_state_ = state_t::IDLE;

                if (current_buff_)
                {
                    free(current_buff_);
                }
                current_buff_ = nullptr;
                return EventHandlerResult::EVENT_CONSUMED;
            } 
            else if (mappedKey.getRaw() >= LM_SLOT_0_KEY && mappedKey.getRaw() <= LM_END_KEYS && keyToggledOn(keyState))
            {
                //Stop the recording and save the recording
                uint8_t macroNumber = mappedKey.getRaw() - LM_SLOT_0_KEY;
                
                //TODO check if macro is smaller than the allocated buffer and then reallocate
                //TODO check there is almost one event recorded. 
                if (keys_[macroNumber])
                {
                    //Selected key has a saved macro. Ask the user to confirm the operation.
                    current_state_ = state_t::ARE_YOU_SURE_TO_OVERWRITE;
                    macro_to_overwrite_ = macroNumber;
                    return EventHandlerResult::EVENT_CONSUMED;
                }

                keys_[macroNumber] = current_buff_;
                current_buff_ = nullptr;

                current_state_ = state_t::IDLE;
                return EventHandlerResult::EVENT_CONSUMED;
            }
            else if (keyToggledOn(keyState) || keyToggledOff(keyState)) 
            {
                //We only listen for key pressed and key released events.
                //If standard key, save the key but let it go to other plugins.
                //A standar key is a non synthetic non reserved and non injected one.
                if (((mappedKey.getFlags() & (SYNTHETIC | RESERVED)) == 0) && ((keyState & INJECTED) == 0))
                {
                    ++(*current_buff_);
                    //Standard key
                    Runtime.serialPort().write("nn\n");
                    if (keyToggledOn(keyState))
                    {
                        //As we know the key is not reserved, use the bit 7 in flags to store if this is a key press or key release.
                        current_buff_[current_buff_pos_++] = mappedKey.getFlags() | LM_KEY_PRESSED;
                        current_buff_[current_buff_pos_++] = mappedKey.getKeyCode();
                    }
                    else
                    {
                        current_buff_[current_buff_pos_++] = mappedKey.getFlags();
                        current_buff_[current_buff_pos_++] = mappedKey.getKeyCode();
                    }
                    //TODO this works only for one slot key, refactor.
                    if ((*current_buff_) == MAX_EVENTS_IN_MACRO)
                    {
                        current_state_ = state_t::MAX_KEYS_REACHED;
                    }
                }
                else
                {
                    Runtime.serialPort().write("ign\n");
                }
            }
            
            return EventHandlerResult::OK;
        break;
        case state_t::MAX_KEYS_REACHED:
            //TODO Refactor this to avoid duplicated code.
            if (mappedKey.getRaw() == LM_START_KEYS && keyToggledOn(keyState))
            {
                //DISCARD CURRENT RECORDING
                
                current_state_ = state_t::IDLE;

                if (current_buff_)
                {
                    free(current_buff_);
                }
                current_buff_ = nullptr;
                return EventHandlerResult::EVENT_CONSUMED;
            } 
            else if (mappedKey.getRaw() >= LM_SLOT_0_KEY && mappedKey.getRaw() <= LM_END_KEYS && keyToggledOn(keyState))
            {
                //Stop the recording and save the recording
                uint8_t macroNumber = mappedKey.getRaw() - LM_SLOT_0_KEY;
                
                //TODO check if macro is smaller than the allocated buffer and then reallocate
                //TODO check there is almost one event recorded. 
                if (keys_[macroNumber])
                {
                    //Selected key has a saved macro. Ask the user to confirm the operation.
                    current_state_ = state_t::ARE_YOU_SURE_TO_OVERWRITE;
                    macro_to_overwrite_ = macroNumber;
                    return EventHandlerResult::EVENT_CONSUMED;
                }

                keys_[macroNumber] = current_buff_;
                current_buff_ = nullptr;

                current_state_ = state_t::IDLE;
                return EventHandlerResult::EVENT_CONSUMED;
            }
            return EventHandlerResult::OK;
        break;
        case state_t::ARE_YOU_SURE_TO_OVERWRITE:
            if (mappedKey.getRaw() == LM_START_KEYS && keyToggledOn(keyState))
            {
                //DISCARD CURRENT RECORDING
                
                current_state_ = state_t::IDLE;

                if (current_buff_)
                {
                    free(current_buff_);
                }
                current_buff_ = nullptr;
                return EventHandlerResult::EVENT_CONSUMED;
            } 
            else if (mappedKey.getRaw() >= LM_SLOT_0_KEY && mappedKey.getRaw() <= LM_END_KEYS && keyToggledOn(keyState))
            {
                //Stop the recording and save the recording
                uint8_t macroNumber = mappedKey.getRaw() - LM_SLOT_0_KEY;
                                 
                if (keys_[macroNumber])
                {
                    if (macroNumber == macro_to_overwrite_)
                    {
                        free(keys_[macro_to_overwrite_]);
                    }
                    else
                    {
                        //The user has selected another key with saved macro
                        macro_to_overwrite_ = macroNumber;
                        return EventHandlerResult::EVENT_CONSUMED;
                    }
                }

                keys_[macroNumber] = current_buff_;
                current_buff_ = nullptr;

                current_state_ = state_t::IDLE;
                return EventHandlerResult::EVENT_CONSUMED;
            }
            else
            {
                //Block the keyboard until discarded, overwritten or saved.
                return EventHandlerResult::EVENT_CONSUMED;
            }
            
        break;
    }
}

// EventHandlerResult LiveMacrosPlugin::onFocusEvent(const char *command)
// {

// }

}
}

Dygma::plugin::LiveMacrosPlugin LiveMacros;