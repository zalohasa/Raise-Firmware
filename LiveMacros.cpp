/* -*- mode: c++ -*-
 * Dygma::plugin::LiveMacros -- Plugin to save and play macros from the keyboard without 
 * the need of any controller software.
 * Copyright (C) 2020  Gonzalo Lopez (zalohasa@gmail.com)
 *
 * This program is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation, version 3.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE. See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include "LiveMacros.h"

#include <Kaleidoscope-EEPROM-Settings.h>
#include "Kaleidoscope-FocusSerial.h"

namespace Dygma{
namespace plugin{

using namespace kaleidoscope;

#define LM_KEY_PRESSED B10000000
#define LM_KEY_PRESSED_MASK B01111111

#define EEPROM_TOTAL_SIZE (MAX_EVENTS_IN_MACRO * 2 * TOTAL_MACROS_IN_EEPROM + TOTAL_MACROS_IN_EEPROM + 4)
#define EEPROM_ONE_MACRO_SIZE (MAX_EVENTS_IN_MACRO * 2 + 1)

//Check the free ram
extern "C" char* sbrk(int incr);

static int freeMemory() {
  char top;
  return &top - reinterpret_cast<char*>(sbrk(0));
}


/**
 * Helper to create LED blinks with different times. The time is specified as the template argument.
 * e.g. blinkLed<300>
 */
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

static bool isFreeMacroPosition(uint8_t macroNumber, uint16_t eeprom_base_addr, uint8_t** ramMacros)
{
    if (macroNumber < TOTAL_MACROS_IN_EEPROM)
    {
        uint16_t eepos = eeprom_base_addr + (EEPROM_ONE_MACRO_SIZE * macroNumber);
        uint8_t macroSize = Runtime.storage().read(eepos);
        if (macroSize > 0 && macroSize <= MAX_EVENTS_IN_MACRO)
        {
            return false;
        }
    }
    else 
    {
        if (ramMacros[macroNumber])
        {
            return false;
        }
    }

    return true;
}

static void saveMacro(uint8_t macroNumber, uint8_t* buffer, uint16_t eeprom_base_addr, uint8_t** ramMacros)
{
    if (macroNumber < TOTAL_MACROS_IN_EEPROM)
    {
        //EEprom macro
        uint16_t eepos = eeprom_base_addr + (EEPROM_ONE_MACRO_SIZE * macroNumber);
        uint8_t currentSize = Runtime.storage().read(eepos);
        
        //Free key
        Runtime.storage().write(eepos, buffer[0]);
        for (uint8_t i = 1; i <= (buffer[0] * 2); ++i)
        {
            Runtime.storage().write(eepos + i, buffer[i]);
        }
        //TODO do the commit
        free(buffer);
    }
    else
    {
        //RAM macro
        //TODO if the key already have a buffer, copy the contents to the current key's buffer, to avoid memory fragmentation.
        if (ramMacros[macroNumber])
        {
            free(ramMacros[macroNumber]);
        }
        ramMacros[macroNumber] = buffer;
    }
}

static bool isRamMacro(uint8_t macroNumber)
{
    return (macroNumber >= TOTAL_MACROS_IN_EEPROM);
}

LiveMacrosPlugin::LiveMacrosPlugin() 
{
    memset(keys_, 0, TOTAL_MACROS);
    for (uint8_t i = 0; i < TOTAL_PLUGIN_KEYS; ++i)
    {
        keys_addrs_[i] = KeyAddr::invalid_state;
    }
}

EventHandlerResult LiveMacrosPlugin::onSetup()
{
    //Size is Events*2*total number of macros + 1 byte for size of each macro + 4bytes at the end for version & checksum (someday)
    eeprom_base_addr_ = ::EEPROMSettings.requestSlice(EEPROM_TOTAL_SIZE);
    //TODO Check eeprom content
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
                        if (!isFreeMacroPosition(i, eeprom_base_addr_, keys_))
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
                //Record key red blinking
                cRGB color = {255, 0, 0};
                cRGB off = {0, 0, 0};
                ::LEDControl.setCrgbAt(keys_addrs_[KEY_START_INDEX], blinkLed<100>(color, off));

                //Macro keys coloring.
                for (uint8_t i = 0; i < TOTAL_MACROS; ++i)
                {
                    color = {149, 255, 0};
                    if (keys_addrs_[i].isValid())
                    {
                        if (!isFreeMacroPosition(i, eeprom_base_addr_, keys_))
                        {
                            color.r = 255;
                            color.g = 0;
                            color.b = 0;
                        }
                        
                        if (current_state_ == state_t::ARE_YOU_SURE_TO_OVERWRITE && macro_to_overwrite_ == i)
                        {
                            cRGB yellow = {209, 220, 27};
                            ::LEDControl.setCrgbAt(keys_addrs_[i], blinkLed<400>(yellow, color));
                        }
                        else
                        {
                            if (isRamMacro(i))
                            {
                                cRGB blue = {0, 0, 255};
                                ::LEDControl.setCrgbAt(keys_addrs_[i], blinkLed<400>(blue, color));
                            }
                            else
                            {
                                ::LEDControl.setCrgbAt(keys_addrs_[i], color);
                            }
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
                    //should never happen
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
                if (macroNumber < TOTAL_MACROS_IN_EEPROM)
                {
                    //EEPROM macro
                    uint16_t eepos = eeprom_base_addr_ + (EEPROM_ONE_MACRO_SIZE * macroNumber);
                    uint8_t macroSize = Runtime.storage().read(eepos++);
                    if (isFreeMacroPosition(macroNumber, eeprom_base_addr_, keys_))
                    {
                        return EventHandlerResult::EVENT_CONSUMED;
                    }

                    for (uint8_t i = 0; i < macroSize; i++)
                    {
                        uint8_t flags = Runtime.storage().read(eepos++);

                        //Check our custom key mask for pressed or released key
                        Key key(Runtime.storage().read(eepos++), (flags & LM_KEY_PRESSED_MASK));
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
                else
                {
                    //RAM macro
                    if (!isFreeMacroPosition(macroNumber, eeprom_base_addr_, keys_))
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
                //TODO check there is almost one event recorded. 

                if (!isFreeMacroPosition(macroNumber, eeprom_base_addr_, keys_))
                {
                    //Macro already saved in key
                    current_state_ = state_t::ARE_YOU_SURE_TO_OVERWRITE;
                    macro_to_overwrite_ = macroNumber;
                    return EventHandlerResult::EVENT_CONSUMED;
                }

                saveMacro(macroNumber, current_buff_, eeprom_base_addr_, keys_);
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
                //TODO check there is almost one event recorded. 

                if (!isFreeMacroPosition(macroNumber, eeprom_base_addr_, keys_))
                {
                    //Macro already saved in key
                    current_state_ = state_t::ARE_YOU_SURE_TO_OVERWRITE;
                    macro_to_overwrite_ = macroNumber;
                    return EventHandlerResult::EVENT_CONSUMED;
                }

                saveMacro(macroNumber, current_buff_, eeprom_base_addr_, keys_);
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

                if (!isFreeMacroPosition(macroNumber, eeprom_base_addr_, keys_) && macro_to_overwrite_ != macroNumber)
                {
                    //The user has selected another key with saved macro
                    macro_to_overwrite_ = macroNumber;
                    return EventHandlerResult::EVENT_CONSUMED;
                }

                saveMacro(macroNumber, current_buff_, eeprom_base_addr_, keys_);
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

EventHandlerResult LiveMacrosPlugin::onFocusEvent(const char *command)
{
    if (::Focus.handleHelp(command, PSTR("lv.map\nlv.mapraw\nlv.clean\nlv.commit\nlv.freeram")))
    return EventHandlerResult::OK;

    if (strncmp_P(command, PSTR("lv."), 3) != 0)
        return EventHandlerResult::OK;

    if (strcmp_P(command + 3, PSTR("map")) == 0) 
    {
        if (::Focus.isEOL()) {
            for (uint16_t i = 0; i < EEPROM_TOTAL_SIZE; i++) {
                uint8_t b;
                b = Runtime.storage().read(eeprom_base_addr_ + i);
                ::Focus.send(b);
            }
        }
    }

    if (strcmp_P(command + 3, PSTR("mapraw")) == 0) 
    {
        if (::Focus.isEOL()) {
            for (uint16_t i = 0; i < EEPROM_TOTAL_SIZE; i++) {
                uint8_t b;
                b = Runtime.storage().read(eeprom_base_addr_ + i);
                Runtime.serialPort().write(b);
            }
        }
    }

    if (strcmp_P(command + 3, PSTR("clean")) == 0) 
    {
        for (uint16_t i = 0; i < EEPROM_TOTAL_SIZE; ++i)
        {
            Runtime.storage().write(eeprom_base_addr_ + i, 0xff);
        }
    }

    if (strcmp_P(command + 3, PSTR("commit")) == 0) 
    {
        Runtime.storage().commit();
    }

    if (strcmp_P(command + 3, PSTR("freeram")) == 0) 
    {
        ::Focus.send(freeMemory());
    }

    return EventHandlerResult::EVENT_CONSUMED;
}

}//namespace plugin
}//namespace dygma

Dygma::plugin::LiveMacrosPlugin LiveMacros;