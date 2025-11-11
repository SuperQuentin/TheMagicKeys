/* 
 * Common constants, variables and functions 
 */ 
#ifndef COMMON
#define COMMON

/*************************************************************************************************
* Includes
*************************************************************************************************/
#include "daisy_seed.h"

using namespace daisy;
using namespace daisy::seed;

/*************************************************************************************************
* Defines
*************************************************************************************************/

// Files on SD card
#define MAX_FILE_NAME_LEN 40
#define MAX_FILE_PATH_LEN 200

#define NB_KEYS           85      // Number of keys and notes.

// Special sounds
#define NB_SPECIAL_SOUNDS           2
#define SOUND_READY_IDX             0
#define SOUND_PROGRAM_CHARGING_IDX  1

// Sounds
#define NB_SOUNDS                   (NB_KEYS + NB_SPECIAL_SOUNDS)

/*************************************************************************************************
* Types
*************************************************************************************************/

// Structure defining a sound (notes or special sounds)
typedef struct
{
    // All ..._pos fields define positions in the buffer g_sample_data.
    size_t first_sample_pos; // Position of the first sample of a note.
    size_t last_sample_pos;  // Position of the last sample of a note.
    size_t nb_samples;       // Number of samples of a note.
    bool playing;            // Define if the note is currently playing (key down).
    size_t cur_playing_pos;  // Define the position of the sample to play.
    bool key_up;             // Define if the note is in the release phase (key up).
    size_t key_up_pos;       // Define the position where the key was released.
    size_t pedal_up_pos;     // Define the position where the pedal was released.
    float volume;            // Define the amplification wich depends on the attack time.
    bool sound_end_soon;     // Define if the note is reaching the end of the sample.
} TSoundData;

/*************************************************************************************************
* Variables 
*************************************************************************************************/
// Daisy Seed hardware
extern DaisySeed      g_hw;

// Variable defining all the notes and special sounds. 
extern TSoundData     g_sounds[NB_SOUNDS];

/*************************************************************************************************
* Functions 
*************************************************************************************************/
extern void toggle_right_led(void);
extern void start_playing_a_note(uint16_t key_index, float amplification);
extern void stop_playing_a_note(uint16_t key_index);

#endif //#ifndef COMMON