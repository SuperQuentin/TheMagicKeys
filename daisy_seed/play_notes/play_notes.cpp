/*************************************************************************************************
* This program:
* - reads wav files from a SD card directory (one wav file per note).
* - loads the wav data samples in external RAM memory (65 Mbytes).
* - plays the notes one by one (a.k.a. the range).
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
#define NB_KEYS                     85      // Number of keys and notes.
#define MAX_NB_SIMULTANEOUS_NOTES   10      // 10 notes at 100% volume can be played without saturation.
#define ATTACK_TIME_MS              10      // Milliseconds
#define RELEASE_TIME_MS             250     // Milliseconds
#define SAMPLE_RATE_HZ              44000   // Hertz
#define RELEASE_NB_SAMPLES          ((SAMPLE_RATE_HZ * RELEASE_TIME_MS) / 1000) 
#define ATTACK_NB_SAMPLES           ((SAMPLE_RATE_HZ * ATTACK_TIME_MS) / 1000)

// Wav files on the SD card
#define MAX_FILE_NAME_LEN 40
#define MAX_FILE_PATH_LEN 200
#define WAV_FILE_PATH "/piano_wav/current"
#define MAX_WAV_DATA_SIZE_BYTES (60*1000*1000)
#define MAX_WAV_DATA_SIZE_WORD (MAX_WAV_DATA_SIZE_BYTES / 2)

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
                // Compute the note signal (with the polyphony factor).
                note_sig_int16 = sample_data[pCurNote->cur_playing_pos] / MAX_NB_SIMULTANEOUS_NOTES;
                note_sig_float = s162f(note_sig_int16);

                // Attack
                // The attack factor avoids a tick sound at the note start.
                if (pCurNote->cur_playing_pos - pCurNote->first_sample_pos < ATTACK_NB_SAMPLES)
                {
                    attack_factor = (float)(pCurNote->cur_playing_pos - pCurNote->first_sample_pos);
                    attack_factor /= (float)ATTACK_NB_SAMPLES;
                    note_sig_float *= attack_factor;
                }

                // Release
                // At the end of the release the notes data are re-initialised.
                // The release factor allows a more natural sound at key release.
                if (pCurNote->released == true)
                {
                    if ( (pCurNote->cur_playing_pos - pCurNote->release_pos > RELEASE_NB_SAMPLES) ||
                         (pCurNote->cur_playing_pos >= pCurNote->last_sample_pos) 
                       )
                    {
                        // End of the note -> Note data re-initialisation.
                        pCurNote->playing = false;
                        pCurNote->released = false;
                        pCurNote->cur_playing_pos = pCurNote->first_sample_pos;
                        release_factor = 0.0;
                    }
                    else
                    {
                        // Release factor
                        release_factor = (float)(pCurNote->release_pos + RELEASE_NB_SAMPLES - pCurNote->cur_playing_pos);
                        release_factor /= (float)RELEASE_NB_SAMPLES;
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

// Main program
int main(void)
{
    // Initialise global variables
    memset(notes, 0, sizeof(notes));
    memset(wav_file_name_list, 0, sizeof(wav_file_name_list));

    // Initialise hardware
    hw.Init();

    // Initialise serial log.
    // With parameter true, wait for the serial line connection.
    hw.StartLog(false);

    // Initialise and mount the SD card
    hw.PrintLine("Mounting SD card...");
    mount_sd_card();

    // Build a sorted list of wav file name (one per note).
    hw.PrintLine("Building the list of wav files...");
    build_wav_file_name_list();

    // Read each wav file and load it in RAM.
    hw.PrintLine("Loading the wav files in RAM...");
    load_wav_files_in_ram();

	// Prepare and start the audio call back
    hw.SetAudioBlockSize(4); // number of samples handled per callback
	hw.SetAudioSampleRate(SaiHandle::Config::SampleRate::SAI_48KHZ);
	hw.StartAudio(AudioCallback);
    
    // Play all notes one by one (the range).
    hw.PrintLine("Play all notes one by one at different speed...");
    uint32_t time_ms = 32;
    bool down = false;
    do
    {    
        for (size_t idx = 0; idx < NB_KEYS; idx++)
        {
            hw.PrintLine("Index of note playing=%d time_ms=%d", idx, time_ms);

            notes[idx].playing = true;
    
            System::Delay(time_ms);

            notes[idx].released = true;

            notes[idx].release_pos = notes[idx].cur_playing_pos;

            System::Delay(time_ms);
        }
        
        // Change of speed
        if (down == true)
        {
            time_ms /= 2;
            if (time_ms <= 32)
            {
                down = false;
            }
        } 
        else
        {
            time_ms *= 2;
            if (time_ms >= 16384)
            {
                down = true;
            }
        }

    } while (true);
}
