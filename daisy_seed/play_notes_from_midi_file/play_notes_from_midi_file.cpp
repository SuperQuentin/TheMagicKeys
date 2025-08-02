/*************************************************************************************************
* This program:
* - reads wav files from a SD card directory (one wav file per note).
* - loads the wav data samples in external RAM memory (65 Mbytes).
* - reads a midi file from the SD card.
* - loads the midi file in RAM memory.
* - interpret and plays the midi file forever.
*
* The release of the key is managed by a linear decrease of the signal amplitude (~250 milliseconds).
* To avoid a click sound at the note start (a.k.a. attack) a linear increase of the signal 
* amplitude is added (~10 milliseconds).
*************************************************************************************************/

/*************************************************************************************************
* Includes
*************************************************************************************************/
#include "daisy_seed.h"
#include "fatfs.h"

using namespace daisy;

/*************************************************************************************************
* Defines
*************************************************************************************************/

// Notes, samples and keys
#define NB_KEYS                     85      // Number of keys and notes.
#define MAX_NB_SIMULTANEOUS_NOTES   10      // 10 notes at 100% volume can be played without saturation.
#define WAV_ENV_START_MS            10      // Wav enveloppe beginning in milliseconds. 
#define WAV_ENV_END_MS              0       // Wav enveloppe end in milliseconds.
#define SAMPLE_RATE_HZ              44000   // Hertz
#define WAV_ENV_START_NB_SAMPLES    ((SAMPLE_RATE_HZ * WAV_ENV_START_MS) / 1000)
#define WAV_ENV_END_NB_SAMPLES      ((SAMPLE_RATE_HZ * WAV_ENV_END_MS) / 1000) 
#define MAX_ATTACK_TIME             10000   // Maximum key velocity.
#define MIN_ATTACK_TIME             300     // Minimum key velocity.

// Files on the SD card
#define MAX_FILE_NAME_LEN 40
#define MAX_FILE_PATH_LEN 200

// Wav files on the SD card
#define WAV_FILE_PATH "/piano_wav/current"
#define MAX_WAV_DATA_SIZE_BYTES (60*1000*1000)
#define MAX_WAV_DATA_SIZE_WORD (MAX_WAV_DATA_SIZE_BYTES / 2)

// MIDI files on the SD card
#define MIDI_FILE_PATH      "/midi"
#define MIDI_FILE_MAX_NB    10
#define MAX_MIDI_FILE_SIZE  (100*1000)

/*************************************************************************************************
* Types
*************************************************************************************************/

// Structure defining a note
typedef struct
{
    // All ..._pos variables define positions in the buffer sample_data.
    size_t first_sample_pos; // Position of the first sample of a note.
    size_t last_sample_pos;  // Position of the last sample of a note.
    size_t nb_samples;       // Number of samples of a note.
    bool playing;            // Define if the note is currently playing (key down).
    size_t cur_playing_pos;  // Define the position of the sample to play.
    bool released;           // Define if the note is in the release phase (key up).
    size_t release_pos;      // Define the position where the key was released.
    float volume;            // Define the amplification wich depends on the attack time.
} TNoteData;

/*************************************************************************************************
* Variables
*************************************************************************************************/
// Daisy hardware
DaisySeed      hw;

// Variables to read and load the wav files
char           wav_file_name_list[NB_KEYS * MAX_FILE_NAME_LEN];

// Buffer in external RAM containing all the samples
int16_t        DSY_SDRAM_BSS sample_data[MAX_WAV_DATA_SIZE_WORD];

// Variable defining all the notes 
TNoteData      notes[NB_KEYS];

// Variables to read and load the midi files
char           midi_file_name_list[MIDI_FILE_MAX_NB * MAX_FILE_NAME_LEN];
uint8_t        nb_midi_files;

// Buffer in external RAM containing the data of a MIDI file.
uint8_t        DSY_SDRAM_BSS midi_file_data[MAX_MIDI_FILE_SIZE];

/*************************************************************************************************
* Code
*************************************************************************************************/

// Audio call back function
static void AudioCallback(AudioHandle::InterleavingInputBuffer in,
    AudioHandle::InterleavingOutputBuffer out,
    size_t                                size)
{
    float sig_float;
    int16_t note_sig_int16;
    float note_sig_float;
    float release_factor;
    float attack_factor;
    TNoteData *pCurNote;

    // Several samples must be generated.
    for(size_t block_idx = 0; block_idx < size; block_idx += 2)
    {
        sig_float = 0; // The signal value.
        
        // Scan all the notes of the notes array.
        for (size_t note_idx = 0; note_idx < NB_KEYS; note_idx++)
        {
            pCurNote = &notes[note_idx];
            
            if (pCurNote->playing == true) // This note is playing.
            {
                // Compute the note signal taking into account: 
                // - the polyphony factor (10 simulatenous notes at max volume without saturation).
                // - the volume which depends on the attack time (key velocity).
                note_sig_int16 = sample_data[pCurNote->cur_playing_pos] / MAX_NB_SIMULTANEOUS_NOTES;
                note_sig_float = s162f(note_sig_int16);
                note_sig_float *= pCurNote->volume;

                // Attack
                // The attack factor avoids a tick sound at the note start.
                // It is a linear wav enveloppe applied at the note start.
                if (pCurNote->cur_playing_pos - pCurNote->first_sample_pos < WAV_ENV_START_NB_SAMPLES)
                {
                    attack_factor = (float)(pCurNote->cur_playing_pos - pCurNote->first_sample_pos);
                    attack_factor /= (float)WAV_ENV_START_NB_SAMPLES;
                    note_sig_float *= attack_factor;
                }

                // Release
                // At the end of the release the notes data are re-initialised.
                // The release factor allows a more natural sound at key release.
                // It is a linear wav enveloppe applied at the note end.
                if (pCurNote->released == true)
                {
                    if ( (pCurNote->cur_playing_pos - pCurNote->release_pos > WAV_ENV_END_NB_SAMPLES) ||
                         (pCurNote->cur_playing_pos >= pCurNote->last_sample_pos) 
                       )
                    {
                        // End of the note -> Note data re-initialisation.
                        pCurNote->playing = false;
                        pCurNote->released = false;
                        release_factor = 0.0;
                    }
                    else
                    {
                        // Release factor
                        release_factor = (float)(pCurNote->release_pos + WAV_ENV_END_NB_SAMPLES - pCurNote->cur_playing_pos);
                        release_factor /= (float)WAV_ENV_END_NB_SAMPLES;
                    }
                    
                    note_sig_float *= release_factor;
 
                } // if (pCurNote->released
                
                // Sum all the note signals (polyphony)
                sig_float += note_sig_float;
                
                // Increment current read position (if end of note not reached).
                if (pCurNote->cur_playing_pos < pCurNote->last_sample_pos)
                {
                    pCurNote->cur_playing_pos++;
                }
            } // if (pCurNote->playing  
        } // for (size_t note_idx
        
        // Left signal out
        out[block_idx] = sig_float;

        // Right signal out
        out[block_idx + 1] = sig_float;
    
    } // for(size_t block_idx
}

// Mount the SD card
void mount_sd_card(void)
{
    FRESULT result;
    static SdmmcHandler sd_card;
    static FatFSInterface fsi;

    // Init SD Card
    SdmmcHandler::Config sd_cfg;
    sd_cfg.Defaults();
    sd_card.Init(sd_cfg);
    
    // Links libdaisy i/o to fatfs driver.
    fsi.Init(FatFSInterface::Config::MEDIA_SD);

    // Mount SD Card
    result = f_mount(&fsi.GetSDFileSystem(), "/", 1);

    if (result != FR_OK)
    {
        hw.PrintLine("f_mount result KO");
    }
}

// Read the wav data of the wav file. Copy the data at RAM address ram_address.
// Return the wav data size (in bytes).
size_t read_wav_file(char *file_name, uint8_t* ram_address)
{
    static FIL SDFile;
    static WavFileInfo wav_file_info;
    size_t bytesRead;
    FRESULT result;
    size_t wav_data_size = 0;

    // Read wav file info (file siye and size to skip to reach sample data)
    result = f_open(&SDFile, file_name, FA_READ);
    if (result == FR_OK)
    {
        result = f_read(&SDFile, (void *)&wav_file_info.raw_data, sizeof(WAV_FormatTypeDef), &bytesRead);
        if (result != FR_OK)
        {
            hw.PrintLine("f_read result KO. result=%d", result);
        }
        f_close(&SDFile);
    }
    else
    {
        hw.PrintLine("f_open result KO. result=%d", result);
    }

    // Read wav file data
    uint32_t file_size = wav_file_info.raw_data.FileSize;
    uint32_t size_to_skip = sizeof(WAV_FormatTypeDef) + wav_file_info.raw_data.SubChunk1Size;

    result = f_open(&SDFile, file_name, FA_READ);

    if (result == FR_OK)
    {
        f_lseek(&SDFile, size_to_skip);

        result = f_read(&SDFile, ram_address, file_size - size_to_skip, &bytesRead);
        if (result != FR_OK)
        {
            hw.PrintLine("f_read result KO. result=%d", result);
        }
    
        f_close(&SDFile);

        wav_data_size = bytesRead;
    }
    else
    {
        hw.PrintLine("f_open result KO. result=%d", result);
    }

    return(wav_data_size);
}

// Build a list of all wav files name in the wav directory of the SD card. 
void build_wav_file_name_list(void)
{
    DIR     dir;
    FRESULT result;
    FILINFO finf;
    size_t file_index;
    char search_path[MAX_FILE_PATH_LEN];
    char index_str[4];
    uint16_t nb_wav_files = 0;

    strcpy(search_path, WAV_FILE_PATH);
    hw.PrintLine("search_path=%s", search_path);

    // Open the directory containing the wav files.
    hw.PrintLine("f_opendir");
    result = f_opendir(&dir, search_path);
    if (result != FR_OK)
    {
       hw.PrintLine("f_opendir result KO. result=%d", result);
       return;
    }
    
    // Read directory element one by one.
    while (true)
    {
        hw.PrintLine("f_readdir");
        result = f_readdir(&dir, &finf);

        if(result != FR_OK || finf.fname[0] == 0)
        {
            hw.PrintLine("f_readdir KO. result=%d", result);
            break;
        }

        // Skip element if its a directory or a hidden file.
        if(finf.fattrib & (AM_HID | AM_DIR))
        {
            hw.PrintLine("Skip element");
            continue;
        }
        
        // Check if its a wav file. If yes, add it to the list.
        hw.PrintLine("finf.fname=%s", finf.fname);

        if(strstr(finf.fname, ".wav") || strstr(finf.fname, ".WAV"))
        {
            hw.PrintLine("Wav file found:%s", finf.fname);
            
            // Compute the file index based on the 3 first characters of the file name
            strncpy(&index_str[0], &finf.fname[0], 3);
            hw.PrintLine("index_str=%s", index_str);
            file_index = atoi(index_str) - 1;
            hw.PrintLine("file_index=%d", file_index);
            
            // Copy the file name at the right file index
            strcpy(&wav_file_name_list[file_index * MAX_FILE_NAME_LEN], finf.fname);
            
            nb_wav_files++;
            hw.PrintLine("nb_wav_files=%ld", nb_wav_files);
        }
        
        if (nb_wav_files >= NB_KEYS)
        {
            hw.PrintLine("Maximum number of files reached");
            break;
        }

    } // End while

    for (size_t idx=0; idx < NB_KEYS; idx++)
    {
        hw.PrintLine("file_name=%s", &wav_file_name_list[idx * MAX_FILE_NAME_LEN]);
    }
    
    f_closedir(&dir);
}

// Build a list of midi file names in the MIDI directory of the SD card. 
void build_midi_file_name_list(void)
{
    DIR     dir;
    FRESULT result;
    FILINFO finf;
    size_t file_index = 0;
    char search_path[MAX_FILE_PATH_LEN];

    strcpy(search_path, MIDI_FILE_PATH);
    hw.PrintLine("search_path=%s", search_path);

    // Open the directory containing the MIDI files.
    hw.PrintLine("f_opendir");
    result = f_opendir(&dir, search_path);
    if (result != FR_OK)
    {
       hw.PrintLine("f_opendir result KO. result=%d", result);
       return;
    }
    
    // Read directory element one by one.
    while (true)
    {
        hw.PrintLine("f_readdir");
        result = f_readdir(&dir, &finf);

        if(result != FR_OK || finf.fname[0] == 0)
        {
            hw.PrintLine("f_readdir KO. result=%d", result);
            break;
        }

        // Skip element if its a directory or a hidden file.
        if(finf.fattrib & (AM_HID | AM_DIR))
        {
            hw.PrintLine("Skip element");
            continue;
        }
        
        // Check if its a MIDI file. If yes, add it to the list.
        hw.PrintLine("finf.fname=%s", finf.fname);

        if(strstr(finf.fname, ".mid") || strstr(finf.fname, ".MID"))
        {
            hw.PrintLine("MIDI file found:%s", finf.fname);
            
            // Copy the file name to the list
            strcpy(&midi_file_name_list[file_index * MAX_FILE_NAME_LEN], finf.fname);
            file_index++;
            
            hw.PrintLine("nb_midi_files=%ld", file_index);
        }

    } // End while
    
    f_closedir(&dir);

    nb_midi_files = file_index;
}

// Read and load the wav file data in external RAM. One file per note.
// Update the notes array with the start and end position in RAM of each note.
void load_wav_files_in_ram(void)
{
    char file_path_and_name[MAX_FILE_PATH_LEN];
    size_t wav_data_size_bytes = 0;
    size_t start_note_word_pos = 0;
    uint8_t* ram_address = NULL;
    TNoteData *pCurNote;

    for (uint16_t file_idx = 0; file_idx < NB_KEYS; file_idx++)
    {
        // Current note data
        pCurNote = &notes[file_idx];

        // Build the full file path name
        strcpy(file_path_and_name, WAV_FILE_PATH);
        strcat(file_path_and_name, "/");
        strcat(file_path_and_name, &wav_file_name_list[file_idx * MAX_FILE_NAME_LEN]);
        hw.PrintLine("file_path_and_name=%s", file_path_and_name);
        
        // Load the wav data at the current note position.
        ram_address = (uint8_t*)(&sample_data[start_note_word_pos]);
        wav_data_size_bytes = read_wav_file(file_path_and_name, ram_address);

        // For each note record the position of the first sample, last sample and number of samples.
        pCurNote->first_sample_pos = start_note_word_pos;
        pCurNote->nb_samples = wav_data_size_bytes / 2; // bytes to word size
        pCurNote->last_sample_pos = pCurNote->first_sample_pos + pCurNote->nb_samples;
        pCurNote->cur_playing_pos = pCurNote->first_sample_pos;

        hw.PrintLine("Note start_position=%d nb_samples=%d", pCurNote->first_sample_pos, pCurNote->nb_samples);

        // Compute the next note position.
        start_note_word_pos += pCurNote->nb_samples;
    }
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
    strcat(file_path_and_name, &midi_file_name_list[file_idx * MAX_FILE_NAME_LEN]);
    hw.PrintLine("file_path_and_name=%s", file_path_and_name);

    result = f_open(&SDFile, file_path_and_name, FA_READ);
    if (result == FR_OK)
    {   file_size = f_size(&SDFile);

        result = f_read(&SDFile, midi_file_data, file_size, &bytesRead);
        if (result != FR_OK)
        {
            hw.PrintLine("f_read result KO. result=%d", result);
        }
        else if (bytesRead != file_size)
        {
            hw.PrintLine("f_read. File not read entirely.");
        }
    
        f_close(&SDFile);
    }
    else
    {
        hw.PrintLine("f_open result KO. result=%d", result);
    }
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

uint16_t u16_from_bytes_big(uint8_t* nb_in)
{
    uint16_t nb_ret = 0;
    nb_ret += (uint16_t) (nb_in[0] << 8);
    nb_ret += (uint16_t) (nb_in[1] << 0);
    return(nb_ret);
}

/*
def decode_var_length_param(data, start_index):
    
    idx = start_index
    rel_idx = 0
    value = 0
    length = 0
    
    # Parse byte per byte
    while(True):
        byte_value = data[idx] & 0x7F
        
        value = value << 7
        value += byte_value
        length += 1
        
        last_byte = (data[idx] & 0x80 == 0)
        if last_byte:
            break # Last byte
        #end if

        idx += 1
        rel_idx += 1
    # end while
    
    return value, length
#end def
*/

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
        hw.PrintLine("Error: last byte not found");
    }
    
    *value = ret_value;
}

/* Toggle the right LED state */
void toggle_right_led(void)
{
    static bool led_state = false; // OFF
    
    led_state = !led_state;

    hw.SetLed(led_state);
}

// Parse the MIDI file from RAM and play the notes.
void play_midi_file_from_ram(void)
{
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
    uint32_t tempo = 500; //[millisec / quarter_note]
    uint8_t key_idx;
    uint8_t velocity;
    TNoteData* pCurNote = NULL;
    uint32_t note_counter;

    // Header
    hw.PrintLine("** HEADER **");

    header_len = u32_from_bytes_big(&midi_file_data[4]);
    hw.PrintLine("header_len=%d", header_len);

    if ((memcmp(&midi_file_data[0], "MThd", 4) != 0) || header_len != 6)
    {
        hw.PrintLine("MIDI parsing error.");
        return;
    }

    file_format = u16_from_bytes_big(&midi_file_data[8]);
    hw.PrintLine("file_format=%d", file_format);

    nb_tracks = u16_from_bytes_big(&midi_file_data[10]);
    hw.PrintLine("nb_tracks=%d", nb_tracks);

    time_unit = u16_from_bytes_big(&midi_file_data[12]);
    hw.PrintLine("time_unit=%d", time_unit);

    idx += 14;

    // Parsing of tracks.
    while(true)
    {
        if (memcmp(&midi_file_data[idx], "MTrk", 4) != 0)
        {
            hw.PrintLine("End of all tracks");
            break;
        }
        idx += 4;
    
        hw.PrintLine("** TRACK CHUNK **");

        track_len = u32_from_bytes_big(&midi_file_data[idx]);
        hw.PrintLine("track_len=%d", track_len);
        idx += 4;
        
        // Parsing of one track.
        start_track_idx = idx;
        note_counter = 0;
        while(true)
        {
            // v_time
            midi_decode_var_length_param(&midi_file_data[idx], &value, &len);
            hw.PrintLine("v_time=%d, len=%d", value, len);
            idx += len;
            
            time_ms = (tempo * value) / ((uint32_t)time_unit);
            System::Delay(time_ms);
            hw.PrintLine("time_ms=%d", time_ms);

            if (midi_file_data[idx] == 0xFF)
            {
                idx += 1;
                hw.PrintLine("META EVENT");
                
                // meta_type
                meta_type = midi_file_data[idx];
                idx += 1;
                hw.PrintLine("meta_type=0x%x", meta_type);
                
                // v_length
                midi_decode_var_length_param(&midi_file_data[idx], &v_length, &len);
                idx += len;
                hw.PrintLine("v_length=%d", v_length);
                
                idx += v_length;
            } 
            else if ((midi_file_data[idx] >= 0xF0) && (midi_file_data[idx] <= 0xF7))
            {
                idx += 1;
                hw.PrintLine("SYSEX EVENT");
            }
            else
            {
                hw.PrintLine("MIDI EVENT");
                
                // Status
                status = midi_file_data[idx];
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
                    data_byte_1 = midi_file_data[idx];
                    idx += 1;
                } // if (nb_data_bytes >= 1)

                if (nb_data_bytes >= 2)
                {
                    data_byte_2 = midi_file_data[idx];
                    idx += 1;
                } // if (nb_data_bytes >= 2)
                
                hw.PrintLine("Command=%s, data_byte_1=%d, data_byte_2=%d, channel_nb=%d", command_str, data_byte_1, data_byte_2, channel_nb);

                if (status_msb == 0x90)
                {
                    // Note_On
                    toggle_right_led();

                    key_idx = data_byte_1;
                    if (key_idx >= NB_KEYS)
                    {
                        key_idx = NB_KEYS;
                    }

                    if (key_idx > 0)
                    {
                        key_idx -= 1;
                    }

                    velocity = data_byte_2;
                    pCurNote = &notes[key_idx];
                    
                    if (velocity != 0)
                    {
                        note_counter++;

                        pCurNote->volume = 1.0;

                        pCurNote->cur_playing_pos = pCurNote->first_sample_pos;
                        pCurNote->playing = true;
                    }
                    else
                    {
                        pCurNote->release_pos = pCurNote->cur_playing_pos;
                        pCurNote->released = true;
                    }
                }
            
            } // if (midi_file_data[idx] == 0xFF)

            if (idx - start_track_idx >= track_len)
            {
                hw.PrintLine("End of a track");
                break; // End of a track
            }
        } // while(true) -> End of a track

    } // while(true) -> End of tracks

} // End of play_midi_file_from_ram

/* Compute the amplification factor which depends on the attack_time
   We compute a linear function such as: 
   - For time <= Tmin -> amp_factor = 1
   - For time >= Tmax -> amp_factor = 0.1
   Thus, the linear function is: amp_factor = a.time + b
   with: 
   a = -0.9 / (Tmax - Tmin)
   b = 1 - a.Tmin
   e.g For Tmin = 300 and TMax = 10000 the function is:
   a = -9.27e-5 and b = 1.027
*/
float compute_volume(uint32_t attack_time)
{
    float amp_factor;
    float slope;
    float offset;
    
    slope  = -0.9 / (float)(MAX_ATTACK_TIME - MIN_ATTACK_TIME);
    offset = 1.0 - slope * (float)MIN_ATTACK_TIME;

    amp_factor = slope * (float)attack_time + offset;
    
    if (amp_factor > 1.0f)
    {
        amp_factor = 1.0f;
    }
    else if (amp_factor < 0.1f)
    {
        amp_factor = 0.1f;
    }

    return amp_factor;
}

/* Main program */
int main(void)
{
    // Initialise global variables
    memset(notes, 0, sizeof(notes));
    memset(wav_file_name_list, 0, sizeof(wav_file_name_list));

    // Initialise hardware
    hw.Init();
    toggle_right_led();

    // Initialise serial log.
    // Set parameter to true to wait for the serial line connection.
    hw.StartLog(true);
    toggle_right_led();

    // Initialise and mount the SD card
    hw.PrintLine("Mounting SD card...");
    toggle_right_led();
    mount_sd_card();

    // Build a sorted list of wav file name (one per note).
    hw.PrintLine("Building the list of wav files...");
    toggle_right_led();
    build_wav_file_name_list();

    // Read each wav file and load it in RAM.
    hw.PrintLine("Loading the wav files in RAM...");
    toggle_right_led();
    load_wav_files_in_ram();

    // Build a list of midi file name to play.
    hw.PrintLine("Building the list of MIDI files...");
    toggle_right_led();
    build_midi_file_name_list();

	// Prepare and start the audio call back
    hw.SetAudioBlockSize(4); // number of samples handled per callback
	hw.SetAudioSampleRate(SaiHandle::Config::SampleRate::SAI_48KHZ);
	hw.StartAudio(AudioCallback);
    
    // Play the MIDI files one by one.
    hw.PrintLine("Play MIDI files...");
    toggle_right_led();

    do
    {
        for (uint8_t file_idx=0; file_idx < nb_midi_files; file_idx++)
        {
            hw.PrintLine("Load a MIDI file in RAM...");
            toggle_right_led();
            load_midi_file_in_ram(file_idx);

            play_midi_file_from_ram();
        }

        hw.DelayMs(10000);
        toggle_right_led();

    } while (true);
    
} // int main(void)
