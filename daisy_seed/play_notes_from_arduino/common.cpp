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