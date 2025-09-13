/*************************************************************************************************
* This program:
* - reads wav files in a SD card directory (one wav file per note).
* - loads the wav data samples in external RAM memory (65 Mbytes).
* - plays the wav files one by one.
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
#define MAX_NB_WAV_FILES 85
#define MAX_FILE_NAME_LEN 40
#define MAX_FILE_PATH_LEN 200
#define WAV_FILE_PATH "/piano_wav/current"
#define MAX_WAV_DATA_SIZE_BYTES (60*1000*1000)
#define MAX_WAV_DATA_SIZE_WORD (MAX_WAV_DATA_SIZE_BYTES / 2)

/*************************************************************************************************
* Variables
*************************************************************************************************/
// Daisy hardware
DaisySeed      hw;

// Variables to read and load the wav files
char           wav_file_name_list[MAX_NB_WAV_FILES * MAX_FILE_NAME_LEN];

// Buffer in external RAM containing all the samples
int16_t        DSY_SDRAM_BSS sample_data[MAX_WAV_DATA_SIZE_WORD];

// Variables to manage samples reading and playing.
size_t         current_sample_pos;
size_t         note_first_sample[MAX_NB_WAV_FILES];
size_t         note_nb_samples[MAX_NB_WAV_FILES];

/*************************************************************************************************
* Code
*************************************************************************************************/

// Audio call back function
static void AudioCallback(AudioHandle::InterleavingInputBuffer in,
    AudioHandle::InterleavingOutputBuffer out,
    size_t                                size)
{
    // Play all the notes 

    float sig;
    for(size_t block_idx = 0; block_idx < size; block_idx += 2)
    {
        sig = s162f(sample_data[current_sample_pos]) * 0.1f;

        // left out
        out[block_idx] = sig;

        // right out
        out[block_idx + 1] = sig;

        // Update read file pointer
        current_sample_pos++;
        if (current_sample_pos >= note_first_sample[MAX_NB_WAV_FILES - 1] + note_nb_samples[MAX_NB_WAV_FILES - 1])
        {
            current_sample_pos = 0;
        }
    }
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

    // Read wav file info (size to skip to read data)
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

// Build a list of all wav files name in the wav directory. 
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

        // Skip if its a directory or a hidden file.
        if(finf.fattrib & (AM_HID | AM_DIR))
        {
            hw.PrintLine("Skip element");
            continue;
        }
        
        // Now we'll check if its .wav and add to the list.
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
        
        if (nb_wav_files >= MAX_NB_WAV_FILES)
        {
            hw.PrintLine("Maximum number of files reached");
            break;
        }

    } // End while

    for (size_t idx=0; idx < MAX_NB_WAV_FILES; idx++)
    {
        hw.PrintLine("file_name=%s", &wav_file_name_list[idx * MAX_FILE_NAME_LEN]);
    }
    
    f_closedir(&dir);
}

// Read and load the wav file data in external RAM. One file per note.
// Build an array containing the start position in RAM of each note.
void load_wav_files_in_ram(void)
{
    char file_path_and_name[MAX_FILE_PATH_LEN];
    size_t wav_data_size_bytes = 0;
    size_t start_note_word_pos = 0;
    uint8_t* ram_address = NULL;

    memset(note_first_sample, 0, sizeof(note_first_sample));
    memset(note_nb_samples, 0, sizeof(note_nb_samples));

    for (uint16_t file_idx = 0; file_idx < MAX_NB_WAV_FILES; file_idx++)
    {
        // Build the full file path name
        strcpy(file_path_and_name, WAV_FILE_PATH);
        strcat(file_path_and_name, "/");
        strcat(file_path_and_name, &wav_file_name_list[file_idx * MAX_FILE_NAME_LEN]);
        hw.PrintLine("file_path_and_name=%s", file_path_and_name);
        
        // Load the wav data at the current note position.
        ram_address = (uint8_t*)(&sample_data[start_note_word_pos]);
        wav_data_size_bytes = read_wav_file(file_path_and_name, ram_address);

        // For each note record the position of the first sample and the number of samples.
        note_first_sample[file_idx] = start_note_word_pos;
        note_nb_samples[file_idx] = wav_data_size_bytes / 2; // bytes to word size
        hw.PrintLine("Note start_position=%d nb_samples=%d", note_first_sample[file_idx], note_nb_samples[file_idx]);

        // Update the note position.
        start_note_word_pos += note_nb_samples[file_idx]; // bytes to word size
    }
}

// Main program
int main(void)
{
    // Initialise hardware
    hw.Init();

    // Wait for serial line connection to log.
    hw.StartLog(true);

    hw.PrintLine("Step 1");

    // Initialise and mount the SD card
    mount_sd_card();

    hw.PrintLine("Step 2");

    // Build a sorted list of wav file name (one per note).
    build_wav_file_name_list();

    // Read each wav file and load it in RAM.
    load_wav_files_in_ram();

    hw.PrintLine("Step 3");

	// Prepare and start audio call back
    current_sample_pos = 0;
    hw.SetAudioBlockSize(4); // number of samples handled per callback
	hw.SetAudioSampleRate(SaiHandle::Config::SampleRate::SAI_48KHZ);
	hw.StartAudio(AudioCallback);

    hw.PrintLine("Step 4");

    while(1);
}