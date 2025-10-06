/*************************************************************************************************
* This program:
* - reads wav files from a SD card directory (one wav file per note).
* - loads the wav data samples in external RAM memory (65 Mbytes).
* - manages a simple communication protocol with the Arduino (note_on, note_off).
* - plays the notes.
* - takey into account the pedal.
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
#define WAV_ENV_START_MS            10      // Wav enveloppe for the attack in milliseconds. 
#define WAV_ENV_END_MS              250     // Wav enveloppe for the release in milliseconds.
#define SAMPLE_RATE_HZ              44000   // Hertz
#define MAX_ATTACK_TIME             100000  // Maximum key velocity (arbitrary unit based on arduino time).
#define MIN_ATTACK_TIME             10000   // Minimum key velocity (arbitrary unit based on arduino time).
#define PEDAL_KEY_IDX               85      // We consider for convenience that the pedal key is the 86th key.
#define WAV_ENV_START_NB_SAMPLES    ((SAMPLE_RATE_HZ * WAV_ENV_START_MS) / 1000) // Conversion from ms to nb of samples
#define WAV_ENV_END_NB_SAMPLES      ((SAMPLE_RATE_HZ * WAV_ENV_END_MS) / 1000)   // Conversion from ms to nb of samples

// Wav files on the SD card
#define MAX_FILE_NAME_LEN 40
#define MAX_FILE_PATH_LEN 200
#define WAV_FILE_PATH "/piano_wav/current"
#define MAX_WAV_DATA_SIZE_BYTES (60*1000*1000) // 60 Mbytes 
#define MAX_WAV_DATA_SIZE_WORD (MAX_WAV_DATA_SIZE_BYTES / 2)

// Message received from arduino
#define MAX_MESSAGE_SIZE 20

/*************************************************************************************************
* Types
*************************************************************************************************/

// Structure defining a note
typedef struct
{
    // All ..._pos fields define positions in the buffer sample_data.
    size_t first_sample_pos; // Position of the first sample of a note.
    size_t last_sample_pos;  // Position of the last sample of a note.
    size_t nb_samples;       // Number of samples of a note.
    bool playing;            // Define if the note is currently playing (key down).
    size_t cur_playing_pos;  // Define the position of the sample to play.
    bool key_up;             // Define if the note is in the release phase (key up).
    size_t key_up_pos;       // Define the position where the key was released.
    size_t pedal_up_pos;     // Define the position where the pedal was released.
    float volume;            // Define the amplification wich depends on the attack time.
    bool note_end_soon;      // Define if the note is reaching the end of the sample.
} TNoteData;

bool pedal_up;               // Define if the pedal is up or down.

// Message received from arduino
typedef enum {KEY_UP_MSG, KEY_DOWN_MSG} e_msg_type;

/*************************************************************************************************
* Variables
*************************************************************************************************/
// Daisy Seed hardware
DaisySeed      hw;

// Variables containing all the wav file names on the SD card.
char           wav_file_name_list[NB_KEYS * MAX_FILE_NAME_LEN];

// Buffer in external RAM containing all the samples
int16_t        DSY_SDRAM_BSS sample_data[MAX_WAV_DATA_SIZE_WORD];

// Variable defining all the notes. 
TNoteData      notes[NB_KEYS];

/*************************************************************************************************
* Local functions declaration
*************************************************************************************************/
uint16_t arduino_to_piano_key_index(uint16_t key_index_arduino);
void toggle_right_led(void);
float compute_volume(uint32_t attack_time);
size_t read_wav_file(char *file_name, uint8_t* ram_address);
void display_all_notes_data(void);

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
    size_t release_pos;

    // Several samples must be generated.
    for(size_t block_idx = 0; block_idx < size; block_idx += 2)
    {
        sig_float = 0.0; // Initialize the signal value.
        
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

                /* Before the note end, we simulate a normal release to avoid a click sound */
                if (   (pCurNote->last_sample_pos - pCurNote->cur_playing_pos <= WAV_ENV_END_NB_SAMPLES)
                    && (pCurNote->note_end_soon == false) )
                {
                    pCurNote->note_end_soon = true;
                    pCurNote->key_up_pos    = pCurNote->cur_playing_pos;
                    pCurNote->pedal_up_pos  = pCurNote->cur_playing_pos;
                }

                /* Release
                   At the end of the release the notes data are re-initialised. The release factor 
                   allows a more natural sound at key release (avoid a click sound).
                   It is a linear wav enveloppe applied at the note end. */
                if (   ((pCurNote->key_up == true) && (pedal_up == true))
                    || (pCurNote->note_end_soon == true) )
                {
                    // Take into account the position when the pedal or the key was up
                    // depending on the time order.
                    if (pCurNote->key_up_pos < pCurNote->pedal_up_pos)
                    {
                        // The pedal was up after the key.
                        release_pos = pCurNote->pedal_up_pos;
                    }
                    else
                    {
                        // The key was up after the pedal.
                        release_pos = pCurNote->key_up_pos;
                    }

                    if (pCurNote->cur_playing_pos - release_pos >= WAV_ENV_END_NB_SAMPLES)
                    {
                        // End of the release or end of the note -> data re-initialisation.
                        pCurNote->cur_playing_pos = pCurNote->first_sample_pos;
                        pCurNote->key_up_pos      = pCurNote->first_sample_pos;
                        pCurNote->pedal_up_pos    = pCurNote->first_sample_pos;
                        pCurNote->playing         = false;
                        pCurNote->key_up          = false;
                        pCurNote->note_end_soon   = false;
                        pCurNote->volume          = 0.0;
                        release_factor            = 0.0;
                    }
                    else
                    {
                        // Compute the release factor
                        release_factor = (float)(release_pos + WAV_ENV_END_NB_SAMPLES - pCurNote->cur_playing_pos);
                        release_factor /= (float)WAV_ENV_END_NB_SAMPLES;
                    }
                    
                    note_sig_float *= release_factor;
 
                } // if (pCurNote->key_up
                
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

/* Initialise global variables */
void initialize_global_variables(void)
{
    pedal_up = true;
    
    // We consider that fields of type bool are false when set to 0.
    memset(notes, 0, sizeof(notes));
    for (size_t idx = 0; idx < NB_KEYS; idx++)
    {
        notes[idx].key_up = true;
        notes[idx].volume = 0.0;
    }
    memset(wav_file_name_list, 0, sizeof(wav_file_name_list));
}

/* Mount the SD card */
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

/* Build a list of all wav files name in the wav directory of the SD card. */
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

/* Read and load the wav file data in external RAM. One file per note.
   Update the notes array fields first_sample_pos, last_sample_pos, nb_samples... */
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
        
        // Initialise some fields with the first sample position.
        pCurNote->cur_playing_pos = pCurNote->first_sample_pos;
        pCurNote->key_up_pos      = pCurNote->first_sample_pos;
        pCurNote->pedal_up_pos    = pCurNote->first_sample_pos;

        hw.PrintLine("Note start_position=%d nb_samples=%d", pCurNote->first_sample_pos, pCurNote->nb_samples);

        // Compute the next note position.
        start_note_word_pos += pCurNote->nb_samples;
    }
}

/* Configure UART */
void initialize_uart(UartHandler* p_uart)
{
    UartHandler::Config config;

    config.baudrate      = 115200;
    config.periph        = UartHandler::Config::Peripheral::USART_1;
    config.stopbits      = UartHandler::Config::StopBits::BITS_1;
    config.parity        = UartHandler::Config::Parity::NONE;
    config.mode          = UartHandler::Config::Mode::TX_RX;
    config.wordlength    = UartHandler::Config::WordLength::BITS_8;
    config.pin_config.rx = {DSY_GPIOB, 7};  // (USART_1 RX) Daisy pin 15
    config.pin_config.tx = {DSY_GPIOB, 6};  // (USART_1 TX) Daisy pin 14    

    p_uart->Init(config);
}

/* Flush UART */
void flush_uart(UartHandler* p_uart)
{
    enum UartHandler::Result result = UartHandler::Result::OK;
    uint8_t char_rec = 0;
    
    while(result == UartHandler::Result::OK)
    {
        result = p_uart->BlockingReceive(&char_rec, 1, 10);
    }
}

/* Wait for a message on UART */
int receive_msg_on_uart(UartHandler* p_uart, char msg_rec[MAX_MESSAGE_SIZE])
{
    enum UartHandler::Result uart_result;
    uint8_t char_rec = 0;
    uint8_t char_idx = 0;
    int result = 0;

    // Wait for the start of the message (character 'S').
    while(1) 
    {
        uart_result = p_uart->BlockingReceive(&char_rec, 1, 0);
        if ((uart_result == UartHandler::Result::OK) && (char_rec == 'S'))
        {
            break;
        }
    }

    // Receive characters until the end of message (character 0x0a).
    while( (char_rec != 0x0a) || (char_idx == MAX_MESSAGE_SIZE))
    {
        uart_result = p_uart->BlockingReceive(&char_rec, 1, 0);
        if (uart_result == UartHandler::Result::OK)
        {
            msg_rec[char_idx] = char_rec;
            if (char_idx < MAX_MESSAGE_SIZE)
            {
                char_idx++;
            }
        } 
    }
    
    if (char_idx == MAX_MESSAGE_SIZE)
    {
        hw.PrintLine("Error: Message too long received");
        result = -1;
    }
    
    msg_rec[char_idx] = 0; // Null terminated string

    return result;
}

/* Analyze messages received from Arduino. */
int analyze_msg_received(char msg_rec[MAX_MESSAGE_SIZE], uint16_t *p_key_index, e_msg_type *p_msg_type, uint32_t* p_time)
{
    int result = 0;
    int result_2 = 0;
    uint8_t msg_len = strlen(msg_rec) - 2; // Remove the 2 end message characters (0x0d and 0x0a).
    char temp_str[MAX_MESSAGE_SIZE];
    int temp_int;
    uint32_t time;
    uint16_t key_index;
    
    *p_time = 0;

    if (msg_rec[0] == 'D')
    {
        // Message type
        *p_msg_type = KEY_DOWN_MSG;

        // Key index
        strncpy(temp_str, &msg_rec[2], 2);
        temp_str[2] = 0;
        result_2 = sscanf(temp_str, "%d", &temp_int);
        key_index = temp_int;
        if (result_2 != 1)
        {
            hw.PrintLine("Error: Problem to convert key_index received");
            result = -1;
        }
        *p_key_index = arduino_to_piano_key_index(key_index);
        
        // Attack time
        strncpy(temp_str, &msg_rec[4], msg_len - 4);
        temp_str[msg_len] = 0;
        result_2 = sscanf(temp_str, "%ld", &time);
        if (result_2 != 1)
        {
            hw.PrintLine("Error: Problem to convert time received");
            result = -1;
        }
        *p_time = time;
    }
    else if (msg_rec[0] == 'U')
    {
        // Message type
        *p_msg_type = KEY_UP_MSG;

        // Key index
        strncpy(temp_str, &msg_rec[2], 2);
        temp_str[2] = 0;
        result_2 = sscanf(temp_str, "%d", &temp_int);
        key_index = temp_int;
        if (result_2 != 1)
        {
            hw.PrintLine("Error: Problem to convert key_index received");
            result = -1;
        }
        *p_key_index = arduino_to_piano_key_index(key_index);
    }
    else
    {
        hw.PrintLine("Error: Unknown message received");
        result = -1;
    }

    return result;
}

/* This function manages the messages received (KEY_UP_MSG, KEY_DOWN_MSG).
   It works in collaboration with function AudioCallback which is called in parallel. 
   Be careful, as AudioCallback is called asynchronously, the order in which variables 
   are set can matter.
*/
void manage_msg_received(uint16_t key_index, e_msg_type msg_type, uint32_t attack_time)
{
    TNoteData *pCurNote;
    uint16_t idx;

    toggle_right_led();

    if (key_index != PEDAL_KEY_IDX)
    {
        // A key from the keyboard has changed state.
        pCurNote = &notes[key_index];
        
        if (msg_type == KEY_DOWN_MSG) 
        {
            // The key is down
            hw.Print("KEY_DOWN index=%d attack_time=%ld", key_index, attack_time);

            pCurNote->volume = compute_volume(attack_time);
            hw.PrintLine(" volume="FLT_FMT3, FLT_VAR3(pCurNote->volume));

            pCurNote->cur_playing_pos = pCurNote->first_sample_pos;
            pCurNote->key_up_pos      = pCurNote->first_sample_pos;
            pCurNote->pedal_up_pos    = pCurNote->first_sample_pos;
            pCurNote->key_up          = false;
            pCurNote->note_end_soon   = false;

            // Start the note playing by the AudioCallback function.
            pCurNote->playing = true;

        } 
        else if (msg_type == KEY_UP_MSG) 
        {
            // The key is up
            hw.PrintLine("KEY_UP index=%d", key_index);
            
            pCurNote->key_up_pos = pCurNote->cur_playing_pos;
            pCurNote->key_up = true;
        }
    } 
    else // key_index == PEDAL_KEY_IDX
    {
        // The pedal has changed state.
        if (msg_type == KEY_DOWN_MSG) 
        {
            // The pedal is down
            hw.PrintLine("PEDAL_DOWN");
            pedal_up = false;
            
            // The position is reinitialised for all notes (different position for ech note).
            for (idx = 0; idx < NB_KEYS; idx++)
            {
                notes[idx].pedal_up_pos = notes[idx].first_sample_pos;
            }
        } 
        else if (msg_type == KEY_UP_MSG) 
        {
            // The pedal is up
            hw.PrintLine("PEDAL_UP");

            // The position when the pedal is up is set for all notes (different positions for 
            // each note).
            for (idx = 0; idx < NB_KEYS; idx++)
            {
                notes[idx].pedal_up_pos = notes[idx].cur_playing_pos;
            }
            pedal_up = true;
        }
    }
}

/*  This function executes the main forever loop of this program. 
    This function:
    - Receives messages for the Arduino.
    - Analyse messages.
    - Manage messages received.
*/
void play_notes_received_from_arduino(UartHandler* p_uart)
{
    char msg_rec[MAX_MESSAGE_SIZE];
    int result = 0;
    uint16_t key_index;
    e_msg_type msg_type;
    uint32_t attack_time;

    // Receive messages from UART
    while(true)
    {
        result = receive_msg_on_uart(p_uart, msg_rec);
        if (result == 0)
        {   
            result = analyze_msg_received(msg_rec, &key_index, &msg_type, &attack_time);

            if (result == 0)
            {
                manage_msg_received(key_index, msg_type, attack_time);
            
            } // if (result == 0)
        } // if (result == 0)
    } // while(true)
}

/* Read the wav data of a wav file. Copy the data at RAM address ram_address.
   Return the wav data size (in bytes). */
size_t read_wav_file(char *file_name, uint8_t* ram_address)
{
    static FIL SDFile;
    static WavFileInfo wav_file_info;
    size_t bytesRead;
    FRESULT result;
    size_t wav_data_size = 0;

    // Read wav file info (file size and size to skip to reach sample data)
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

/* The Arduino manages 7 keys per satellite board but only 6 piano keys are systematically connected. 
   For 2 boards the 7th key is connected:
   - Board 0:  Arduino key 6 is mapped to piano key 0 which is the leftmost key.
   - Board 6:  Arduino key 48 is mapped to piano key 85 which is the pedal.
   This function does the mapping between Arduino keys and Piano keys.
   Board:         0                     1                       12                    13
   Arduino keys:  0  1  2  3  4  5  6   7  8  9 10 11 12 13 ... 84 85 86 87 88 89 90  91 92 93 94 95 96 97 
   Piano keys:    1  2  3  4  5  6  0   7  8  9 10 11 12 NC ... 73 74 75 76 77 78 NC  79 80 81 82 83 84 NC
   NC stands for Not Connected. 
   */
uint16_t arduino_to_piano_key_index(uint16_t key_index_arduino)
{
    uint16_t key_index_piano; 
    
    if (key_index_arduino == 6)
    {
        key_index_piano = 0;
    } 
    else if (key_index_arduino == 48)
    {
        key_index_piano = PEDAL_KEY_IDX;
    } 
    else 
    {
        key_index_piano = key_index_arduino + 1 - (key_index_arduino / 7);
    }
    
    return(key_index_piano);
}

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

/* Toggle the right LED state */
void toggle_right_led(void)
{
    static bool led_state = false; // OFF
    
    led_state = !led_state;

    hw.SetLed(led_state);
}

/* Display data of a note. Useful for debugging.
   Positions are displayed relatively to the first position.*/
void display_note_data(uint16_t idx) 
{
    size_t first_pos = notes[idx].first_sample_pos;

    hw.Print("idx=%d ", idx);
    hw.Print("playing=%d ", notes[idx].playing);
    hw.Print("key_up=%d ", notes[idx].key_up);
    // hw.Print("first_pos=%d ", notes[idx].first_sample_pos);
    hw.Print("last_pos=%d ", notes[idx].last_sample_pos - first_pos);
    hw.Print("cur_pos=%d ", notes[idx].cur_playing_pos - first_pos);
    hw.Print("kup_pos=%d ", notes[idx].key_up_pos - first_pos);
    hw.PrintLine("pup_pos=%d", notes[idx].pedal_up_pos - first_pos);
}

/* Display data of all notes. Useful for debugging.
   Positions are displayed relatively to the first position.*/
void display_all_notes_data(void)
{
    for( uint16_t idx=0; idx<NB_KEYS; idx++)
    {
        display_note_data(idx);
    }
}

/* Main program */
int main(void)
{
    UartHandler uart;

    // Initialise global variables
    initialize_global_variables();

    // Initialise hardware
    hw.Init();
    toggle_right_led();

    // Initialise serial log.
    // Set parameter to true to wait for the serial line connection.
    hw.StartLog(false);
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

    // Initialize UART
    hw.PrintLine("Initializing UART...");
    toggle_right_led();
    initialize_uart(&uart);
    flush_uart(&uart);

	// Prepare and start the audio call back
    hw.PrintLine("Preparing and starting audio call back ...");
    toggle_right_led();
    hw.SetAudioBlockSize(4); // number of samples handled per callback
	hw.SetAudioSampleRate(SaiHandle::Config::SampleRate::SAI_48KHZ);
	hw.StartAudio(AudioCallback);
    
    // Play notes received from arduino.
    hw.PrintLine("Playing notes received from arduino...");
    toggle_right_led();
    play_notes_received_from_arduino(&uart);

} // int main(void)
