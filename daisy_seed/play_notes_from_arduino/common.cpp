/* 
 * Common constants, variables and functions 
 */ 

/*************************************************************************************************
* Includes
*************************************************************************************************/
#include "daisy_seed.h"
#include "common.h"

using namespace daisy;
using namespace daisy::seed;

/*************************************************************************************************
* Variables
*************************************************************************************************/
// Daisy Seed hardware
DaisySeed      g_hw;

// Variable defining all the notes and special sounds. 
TSoundData     g_sounds[NB_SOUNDS];

/*************************************************************************************************
* Functions implementation
*************************************************************************************************/
/* Toggle the right LED state */
void toggle_right_led(void)
{
    static bool led_state = false; // OFF
    
    led_state = !led_state;

    g_hw.SetLed(led_state);
}

/* Start playing a note with a certain amplification factor from 0.0 to 1.0. */
void start_playing_a_note(uint16_t key_index, float amplification)
{
    TSoundData *pCurNote;
    pCurNote = &g_sounds[NB_SPECIAL_SOUNDS + key_index];

    pCurNote->volume = amplification;

    pCurNote->cur_playing_pos = pCurNote->first_sample_pos;
    pCurNote->key_up_pos      = pCurNote->first_sample_pos;
    pCurNote->pedal_up_pos    = pCurNote->first_sample_pos;
    pCurNote->key_up          = false;
    pCurNote->sound_end_soon  = false;

    // Start the note playing by the AudioCallback function.
    pCurNote->playing = true;
}

/* Stop playing a note. */
void stop_playing_a_note(uint16_t key_index)
{
    TSoundData *pCurNote;
    pCurNote = &g_sounds[NB_SPECIAL_SOUNDS + key_index];

    pCurNote->key_up_pos = pCurNote->cur_playing_pos;
    pCurNote->key_up = true;
}
