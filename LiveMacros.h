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
#pragma once

#include <Kaleidoscope.h>
#include <Kaleidoscope-LEDControl.h>
#include <Kaleidoscope-Ranges.h>

#define LM_RECORD Key(kaleidoscope::ranges::KALEIDOSCOPE_SAFE_START)
#define LM_M(n) Key(kaleidoscope::ranges::KALEIDOSCOPE_SAFE_START + 1 + (n))

#define MAX_EVENTS_IN_MACRO 14 //This number is the number of events (a key press is one event, a key release is one event). Total keys is this number/2
#define TOTAL_MACROS 8 //This is the number of supported macros
#define TOTAL_MACROS_IN_EEPROM 6 //Of the TOTAL_MACROS, how many of them will be saved in EEPROM

#define LAST_EEPROM_MACRO_KEY (TOTAL_MACROS_IN_EEPROM - 1) //This is the last key with the macro saved in eeprom. Following keys will have the macro only in ram (volatile macro)
#define TOTAL_PLUGIN_KEYS (TOTAL_MACROS + 1) //Total number of keys this plugins manages. (physical keys)
#define KEY_START_INDEX TOTAL_MACROS //Index in the physical keys array of the start (record) key (must come after all the macro keys)

static_assert (LAST_EEPROM_MACRO_KEY < TOTAL_MACROS, "Invalid number of last eeprom key");

namespace Dygma{
namespace plugin{

enum LM_Keys : uint16_t
{
    LM_START_KEYS = kaleidoscope::ranges::KALEIDOSCOPE_SAFE_START,
    LM_SLOT_0_KEY = kaleidoscope::ranges::KALEIDOSCOPE_SAFE_START + 1,
    LM_END_KEYS = kaleidoscope::ranges::KALEIDOSCOPE_SAFE_START + 1 + TOTAL_MACROS
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
    kaleidoscope::EventHandlerResult onFocusEvent(const char *command);
private:
    state_t current_state_                  = state_t::IDLE;
    KeyAddr keys_addrs_[TOTAL_PLUGIN_KEYS];
    uint8_t* current_buff_                  = nullptr;
    uint8_t* keys_[TOTAL_MACROS];
    uint16_t eeprom_base_addr_              = 0;
    uint8_t current_buff_pos_               = 0;
    uint8_t macro_to_overwrite_             = 0;
    bool initialized_keys_                  = false;

};

}
}

extern Dygma::plugin::LiveMacrosPlugin LiveMacros;