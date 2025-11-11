/* 
 *  Header file of play_midi_files.c. See this file for more details
 */ 
#ifndef PLAY_MIDI_FILES
#define PLAY_MIDI_FILES

/*************************************************************************************************
* Includes
*************************************************************************************************/
#include "daisy_seed.h"

/*************************************************************************************************
* Defines
*************************************************************************************************/
#define PLAY_ALL_TRACKS 0xFFFF
#define PLAY_ALL_NOTES  0xFFFFFFFF

/*************************************************************************************************
* Functions
*************************************************************************************************/
extern void play_all_midi_files(void);
extern void play_one_midi_file(uint8_t file_idx, uint16_t nb_tracks_to_play, 
                               uint32_t nb_notes_to_play);

#endif //#ifndef PLAY_MIDI_FILES