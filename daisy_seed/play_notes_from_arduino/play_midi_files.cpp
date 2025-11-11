/*
 * This module manages MIDI files. 
 */

/*************************************************************************************************
* Includes
*************************************************************************************************/
#include "daisy_seed.h"
#include "common.h"
#include "play_midi_files.h"

using namespace daisy;
using namespace daisy::seed;

/*************************************************************************************************
* Defines
*************************************************************************************************/
// MIDI files on the SD card
#define MIDI_FILE_PATH      "/midi"
#define MIDI_FILE_MAX_NB    10
#define MAX_MIDI_FILE_SIZE  (100*1000)

/*************************************************************************************************
* Variables
*************************************************************************************************/

// Variables to read and load the midi files
char           g_midi_file_name_list[MIDI_FILE_MAX_NB * MAX_FILE_NAME_LEN];
uint8_t        g_nb_midi_files;

// Buffer in external RAM containing the data of a MIDI file.
uint8_t        DSY_SDRAM_BSS g_midi_file_data[MAX_MIDI_FILE_SIZE];

/*************************************************************************************************
* Functions implementation
*************************************************************************************************/
// Build a list of midi file names in the MIDI directory of the SD card. 
void build_midi_file_name_list(void)
{
    DIR     dir;
    FRESULT result;
    FILINFO finf;
    size_t file_index = 0;
    char search_path[MAX_FILE_PATH_LEN];

    strcpy(search_path, MIDI_FILE_PATH);
    g_hw.PrintLine("search_path=%s", search_path);

    // Open the directory containing the MIDI files.
    g_hw.PrintLine("f_opendir");
    result = f_opendir(&dir, search_path);
    if (result != FR_OK)
    {
       g_hw.PrintLine("f_opendir result KO. result=%d", result);
       return;
    }
    
    // Read directory element one by one.
    while (true)
    {
        g_hw.PrintLine("f_readdir");
        result = f_readdir(&dir, &finf);

        if(result != FR_OK || finf.fname[0] == 0)
        {
            g_hw.PrintLine("f_readdir KO. result=%d", result);
            break;
        }

        // Skip element if its a directory or a hidden file.
        if(finf.fattrib & (AM_HID | AM_DIR))
        {
            g_hw.PrintLine("Skip element");
            continue;
        }
        
        // Check if its a MIDI file. If yes, add it to the list.
        g_hw.PrintLine("finf.fname=%s", finf.fname);

        if(strstr(finf.fname, ".mid") || strstr(finf.fname, ".MID"))
        {
            g_hw.PrintLine("MIDI file found:%s", finf.fname);
            
            // Copy the file name to the list
            strcpy(&g_midi_file_name_list[file_index * MAX_FILE_NAME_LEN], finf.fname);
            file_index++;
            
            g_hw.PrintLine("g_nb_midi_files=%ld", file_index);
        }

    } // End while
    
    f_closedir(&dir);

    g_nb_midi_files = file_index;
}

// Read and load the midi file data in external RAM.
void load_midi_file_in_ram(uint8_t file_idx)
{
    char file_path_and_name[MAX_FILE_PATH_LEN];
    static FIL SDFile;
    FRESULT result;
    size_t file_size;
    size_t bytesRead;

    // Build the full file path name
    strcpy(file_path_and_name, MIDI_FILE_PATH);
    strcat(file_path_and_name, "/");
    strcat(file_path_and_name, &g_midi_file_name_list[file_idx * MAX_FILE_NAME_LEN]);
    g_hw.PrintLine("file_path_and_name=%s", file_path_and_name);

    result = f_open(&SDFile, file_path_and_name, FA_READ);
    if (result == FR_OK)
    {   file_size = f_size(&SDFile);

        result = f_read(&SDFile, g_midi_file_data, file_size, &bytesRead);
        if (result != FR_OK)
        {
            g_hw.PrintLine("f_read result KO. result=%d", result);
        }
        else if (bytesRead != file_size)
        {
            g_hw.PrintLine("f_read. File not read entirely.");
        }
    
        f_close(&SDFile);
    }
    else
    {
        g_hw.PrintLine("f_open result KO. result=%d", result);
    }
}

uint16_t u16_from_bytes_big(uint8_t* nb_in)
{
    uint16_t nb_ret = 0;
    nb_ret += (uint16_t) (nb_in[0] << 8);
    nb_ret += (uint16_t) (nb_in[1] << 0);
    return(nb_ret);
}

uint32_t u32_from_bytes_big(uint8_t* nb_in)
{
    uint32_t nb_ret = 0;
    nb_ret += (uint32_t) (nb_in[0] << 24);
    nb_ret += (uint32_t) (nb_in[1] << 16);
    nb_ret += (uint32_t) (nb_in[2] << 8);
    nb_ret += (uint32_t) (nb_in[3] << 0);
    return(nb_ret);
}

// Decode a variable length parameter (max value on 32 bits).
// For each byte: 
// - the value is coded on the 7 lsb.
// - The msb is set except for the last byte. 
void midi_decode_var_length_param(uint8_t* data, uint32_t* value, uint8_t* len)
{   
    uint8_t byte_value;
    uint32_t ret_value = 0;
    bool last_byte = false;
    
    // Parse byte per byte (max 4 bytes).
    *len = 0;
    for (uint8_t idx = 0; idx < 4; idx++)
    {
        byte_value = data[idx] & 0x7F;

        ret_value = ret_value << 7;
        ret_value += byte_value;

        last_byte = ((data[idx] & 0x80) == 0);
        if (last_byte == true)
        {
            *len = idx + 1;
            break;
        }
    } // End for
    
    if (last_byte == false)
    {
        g_hw.PrintLine("Error: last byte not found");
    }
    
    *value = ret_value;
}

/* Parse the MIDI file from RAM and play the notes. 
Play the number of tracks and notes desired.
*/
void play_midi_file_from_ram(uint16_t nb_tracks_to_play, uint32_t nb_notes_to_play)
{
    uint32_t tempo = 500; //[millisec / quarter_note]
    uint8_t shift_notes = 24;
    uint32_t idx = 0;
    uint32_t header_len;
    uint16_t file_format;
    uint16_t nb_tracks;
    uint16_t time_unit;
    uint32_t track_len;
    uint32_t start_track_idx;
    uint32_t value;
    uint8_t len;
    uint8_t meta_type;
    uint32_t v_length;
    uint8_t status;
    uint8_t status_msb;
    uint8_t channel_nb;
    uint8_t running_status = 0;
    uint8_t running_channel_nb = 0;
    uint8_t nb_data_bytes;
    char command_str[10];
    uint8_t data_byte_1 = 0;
    uint8_t data_byte_2 = 0;
    uint32_t time_ms;
    uint8_t key_idx;
    uint8_t velocity;
    uint32_t note_counter;
    uint16_t track_counter;
    bool stop_playing;

    // Header
    g_hw.PrintLine("** HEADER **");

    header_len = u32_from_bytes_big(&g_midi_file_data[4]);
    g_hw.PrintLine("header_len=%d", header_len);

    if ((memcmp(&g_midi_file_data[0], "MThd", 4) != 0) || header_len != 6)
    {
        g_hw.PrintLine("MIDI parsing error.");
        return;
    }

    file_format = u16_from_bytes_big(&g_midi_file_data[8]);
    g_hw.PrintLine("file_format=%d", file_format);

    nb_tracks = u16_from_bytes_big(&g_midi_file_data[10]);
    g_hw.PrintLine("nb_tracks=%d", nb_tracks);

    time_unit = u16_from_bytes_big(&g_midi_file_data[12]);
    g_hw.PrintLine("time_unit=%d", time_unit);

    idx += 14;

    // Parsing of tracks.
    stop_playing = false;
    track_counter = 0;
    while(!stop_playing)
    {
        if (track_counter > nb_tracks_to_play)
        {
            stop_playing = true;
        }

        if (memcmp(&g_midi_file_data[idx], "MTrk", 4) != 0)
        {
            g_hw.PrintLine("End of all tracks");
            break;
        }
        idx += 4;
    
        g_hw.PrintLine("** TRACK CHUNK **");

        track_len = u32_from_bytes_big(&g_midi_file_data[idx]);
        g_hw.PrintLine("track_len=%d", track_len);
        idx += 4;
        
        // Parsing of one track.
        start_track_idx = idx;
        note_counter = 0;
        while(!stop_playing)
        {
            if (note_counter >= nb_notes_to_play)
            {
                stop_playing = true;
            }

            // v_time
            midi_decode_var_length_param(&g_midi_file_data[idx], &value, &len);
            g_hw.PrintLine("v_time=%d, len=%d", value, len);
            idx += len;
            
            time_ms = (tempo * value) / ((uint32_t)time_unit);
            System::Delay(time_ms);
            g_hw.PrintLine("time_ms=%d", time_ms);

            if (g_midi_file_data[idx] == 0xFF)
            {
                idx += 1;
                g_hw.PrintLine("META EVENT");
                
                // meta_type
                meta_type = g_midi_file_data[idx];
                idx += 1;
                g_hw.PrintLine("meta_type=0x%x", meta_type);
                
                // v_length
                midi_decode_var_length_param(&g_midi_file_data[idx], &v_length, &len);
                idx += len;
                g_hw.PrintLine("v_length=%d", v_length);
                
                idx += v_length;
            } 
            else if ((g_midi_file_data[idx] >= 0xF0) && (g_midi_file_data[idx] <= 0xF7))
            {
                idx += 1;
                g_hw.PrintLine("SYSEX EVENT");
            }
            else
            {
                g_hw.PrintLine("MIDI EVENT");
                
                // Status
                status = g_midi_file_data[idx];
                idx += 1;
                
                status_msb = status & 0xF0;
                channel_nb = status & 0x0F;

                // The status can be omitted if it is the same as for the previous status
                if ((status_msb >= 0x80) &&  (status_msb <= 0xE0))
                {
                    running_status = status_msb;
                    running_channel_nb = channel_nb;
                }
                else
                {
                    status_msb = running_status;
                    channel_nb = running_channel_nb;
                    idx -= 1;
                }

                nb_data_bytes = 0;
                if (status_msb == 0x80)
                {
                    strcpy(command_str, "Note_Off");
                    nb_data_bytes = 2;
                }
                else if (status_msb == 0x90)
                {
                    strcpy(command_str, "Note_On");
                    nb_data_bytes = 2; 
                }
                else if (status_msb == 0xA0)
                {
                    strcpy(command_str, "Poly");
                    nb_data_bytes = 2;
                }
                else if (status_msb == 0xB0)
                {
                    strcpy(command_str, "Ctrl");
                    nb_data_bytes = 2;
                }
                else if (status_msb == 0xC0)
                {
                    strcpy(command_str, "Prog");
                    nb_data_bytes = 1;
                }
                else if (status_msb == 0xD0)
                {
                    strcpy(command_str, "Channel");
                    nb_data_bytes = 1;
                }
                else if (status_msb == 0xE0)
                {
                    strcpy(command_str, "Pitch");
                    nb_data_bytes = 2;
                }
            
                // Data bytes
                if (nb_data_bytes >= 1)
                {
                    data_byte_1 = g_midi_file_data[idx];
                    idx += 1;
                } // if (nb_data_bytes >= 1)

                if (nb_data_bytes >= 2)
                {
                    data_byte_2 = g_midi_file_data[idx];
                    idx += 1;
                } // if (nb_data_bytes >= 2)
                
                g_hw.PrintLine("Command=%s, data_byte_1=%d, data_byte_2=%d, channel_nb=%d", command_str, data_byte_1, data_byte_2, channel_nb);

                if (status_msb == 0x90)
                {
                    // Note_On
                    toggle_right_led();
                    
                    key_idx = data_byte_1;
                    if (key_idx >= shift_notes)
                    {
                        key_idx -= shift_notes;
                    }
                    if (key_idx >= NB_KEYS)
                    {
                        key_idx = NB_KEYS; 
                    }

                    if (key_idx > 0)
                    {
                        key_idx -= 1;
                    }

                    velocity = data_byte_2;
                    
                    if (velocity != 0)
                    {
                        note_counter++;
                        // We consider that velocity = 80 corresponds to max amplification (= 1.0).
                        start_playing_a_note(key_idx, velocity / 80.0);
                    }
                    else
                    {
                        stop_playing_a_note(key_idx);
                    }
                }
            
            } // if (g_midi_file_data[idx] == 0xFF)

            if (idx - start_track_idx >= track_len)
            {
                g_hw.PrintLine("End of a track");
                track_counter++;
                break; // End of a track
            }

        } // while(!stop_playing) -> End of a track

    } // while(!stop_playing) -> End of tracks
}

/* Play one midi file at the given index in the list. 
   Play the number of tracks and notes desired.
*/
void play_one_midi_file(uint8_t file_idx, uint16_t nb_tracks_to_play, 
                        uint32_t nb_notes_to_play)
{
    // Build a list of midi file name to play.
    g_hw.PrintLine("Building the list of MIDI files...");
    toggle_right_led();
    build_midi_file_name_list();

    g_hw.PrintLine("Load a MIDI file in RAM...");
    toggle_right_led();
    load_midi_file_in_ram(file_idx);

    g_hw.PrintLine("Play a MIDI file...");
    play_midi_file_from_ram(nb_tracks_to_play, nb_notes_to_play);
}

/* Play all the midi files */
void play_all_midi_files(void)
{
    // Build a list of midi file name to play.
    g_hw.PrintLine("Building the list of MIDI files...");
    toggle_right_led();
    build_midi_file_name_list();

    for (uint8_t file_idx=0; file_idx < g_nb_midi_files; file_idx++)
    {
        g_hw.PrintLine("Load a MIDI file in RAM...");
        toggle_right_led();
        load_midi_file_in_ram(file_idx);

        g_hw.PrintLine("Play a MIDI file...");
        play_midi_file_from_ram(PLAY_ALL_TRACKS, PLAY_ALL_NOTES);
    }
}
