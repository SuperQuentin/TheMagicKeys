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
using namespace daisy::seed;

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

// Special sounds
#define NB_SPECIAL_SOUNDS           2
#define SOUND_READY_IDX             0
#define SOUND_PROGRAM_CHARGING_IDX  1

// Sounds
#define NB_SOUNDS                   (NB_KEYS + NB_SPECIAL_SOUNDS)

// Wav files on the SD card
#define MAX_FILE_NAME_LEN 40
#define MAX_FILE_PATH_LEN 200
#define WAV_NOTES_BASE_FILE_PATH "/piano_wav"
#define WAV_SPECIAL_SOUNDS_FILE_PATH "/piano_wav/special"
#define MAX_WAV_DATA_SIZE_BYTES (60*1000*1000) // 60 Mbytes 
#define MAX_WAV_DATA_SIZE_WORD (MAX_WAV_DATA_SIZE_BYTES / 2)

// File defining the current program
#define CURRENT_PROG_FILE_PATH "/piano_wav/current_prog"

// Message received from arduino
#define MAX_MESSAGE_SIZE 20

// Enable/Disable logs when key up or down (value: 0 or 1).
#define ENABLED_ALL_LOGS 1

// Wait or not for the uart host connection (value: 0 or 1).
#define WAIT_UART_HOST_CONNECTION_TO_START 0

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

// Message received from arduino
typedef enum {KEY_UP_MSG, KEY_DOWN_MSG} e_msg_type;

/*************************************************************************************************
* Variables
*************************************************************************************************/
// Daisy Seed hardware
DaisySeed      g_hw;

// Variables containing all the notes wav file names on the SD card.
char           g_wav_notes_file_name_list[NB_KEYS * MAX_FILE_NAME_LEN];

// Variables containing all the special sounds wav file names on the SD card.
char           g_wav_special_sounds_file_name_list[NB_SPECIAL_SOUNDS * MAX_FILE_NAME_LEN];

// Buffer in external RAM containing all the samples
int16_t        DSY_SDRAM_BSS g_sample_data[MAX_WAV_DATA_SIZE_WORD];

// Variable defining all the notes and special sounds. 
TSoundData     g_sounds[NB_SOUNDS];

// Define if the pedal is up or down.
bool g_pedal_up;               

// Variable defining the position of the first note in g_sample_data buffer.
// Notes are after special sounds. This variable does not depend on the 
// program selected because special sounds have always the same size.
size_t g_first_note_position;

/*************************************************************************************************
* Local functions declaration
*************************************************************************************************/
uint16_t arduino_to_piano_key_index(uint16_t key_index_arduino);
void toggle_right_led(void);
float compute_volume(uint32_t attack_time);
size_t read_wav_file(char *file_name, uint8_t* ram_address);
void display_all_sounds_data(void);
uint8_t read_current_program(void);
void write_current_program(uint8_t prog_idx);
void play_special_sound(uint8_t sound_idx);

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
    TSoundData *pCurSounds;
    size_t release_pos;

    // Several samples must be generated.
    for(size_t block_idx = 0; block_idx < size; block_idx += 2)
    {
        sig_float = 0.0; // Initialize the signal value.
        
        // Scan all the sounds of the g_sounds array.
        for (size_t sound_idx = 0; sound_idx < NB_SOUNDS; sound_idx++)
        {
            pCurSounds = &g_sounds[sound_idx];
            
            if (pCurSounds->playing == true) // This sound is playing.
            {
                // Compute the note signal taking into account: 
                // - the polyphony factor (10 simulatenous notes at max volume without saturation).
                // - the volume which depends on the attack time (key velocity).
                note_sig_int16 = g_sample_data[pCurSounds->cur_playing_pos] / MAX_NB_SIMULTANEOUS_NOTES;
                note_sig_float = s162f(note_sig_int16);
                note_sig_float *= pCurSounds->volume;

                // Attack
                // The attack factor avoids a tick sound at the note start.
                // It is a linear wav enveloppe applied at the note start.
                if (pCurSounds->cur_playing_pos - pCurSounds->first_sample_pos < WAV_ENV_START_NB_SAMPLES)
                {
                    attack_factor = (float)(pCurSounds->cur_playing_pos - pCurSounds->first_sample_pos);
                    attack_factor /= (float)WAV_ENV_START_NB_SAMPLES;
                    note_sig_float *= attack_factor;
                }

                /* Before the note end, we simulate a normal release to avoid a click sound */
                if (   (pCurSounds->last_sample_pos - pCurSounds->cur_playing_pos <= WAV_ENV_END_NB_SAMPLES)
                    && (pCurSounds->sound_end_soon == false) )
                {
                    pCurSounds->sound_end_soon = true;
                    pCurSounds->key_up_pos     = pCurSounds->cur_playing_pos;
                    pCurSounds->pedal_up_pos   = pCurSounds->cur_playing_pos;
                }

                /* Release
                   At the end of the release the notes data are re-initialised. The release factor 
                   allows a more natural sound at key release (avoid a click sound).
                   It is a linear wav enveloppe applied at the note end. */
                if (   ((pCurSounds->key_up == true) && (g_pedal_up == true))
                    || (pCurSounds->sound_end_soon == true) )
                {
                    // Take into account the position when the pedal or the key was up
                    // depending on the time order.
                    if (pCurSounds->key_up_pos < pCurSounds->pedal_up_pos)
                    {
                        // The pedal was up after the key.
                        release_pos = pCurSounds->pedal_up_pos;
                    }
                    else
                    {
                        // The key was up after the pedal.
                        release_pos = pCurSounds->key_up_pos;
                    }

                    if (pCurSounds->cur_playing_pos - release_pos >= WAV_ENV_END_NB_SAMPLES)
                    {
                        // End of the release or end of the note -> data re-initialisation.
                        pCurSounds->cur_playing_pos = pCurSounds->first_sample_pos;
                        pCurSounds->key_up_pos      = pCurSounds->first_sample_pos;
                        pCurSounds->pedal_up_pos    = pCurSounds->first_sample_pos;
                        pCurSounds->playing         = false;
                        pCurSounds->key_up          = false;
                        pCurSounds->sound_end_soon  = false;
                        pCurSounds->volume          = 0.0;
                        release_factor              = 0.0;
                    }
                    else
                    {
                        // Compute the release factor
                        release_factor = (float)(release_pos + WAV_ENV_END_NB_SAMPLES - pCurSounds->cur_playing_pos);
                        release_factor /= (float)WAV_ENV_END_NB_SAMPLES;
                    }
                    
                    note_sig_float *= release_factor;
 
                } // if (pCurSounds->key_up
                
                // Sum all the note signals (polyphony)
                sig_float += note_sig_float;
                
                // Increment current read position (if end of note not reached).
                if (pCurSounds->cur_playing_pos < pCurSounds->last_sample_pos)
                {
                    pCurSounds->cur_playing_pos++;
                }
            } // if (pCurSounds->playing  
        } // for (size_t sound_idx
        
        // Left signal out
        out[block_idx] = sig_float;

        // Right signal out
        out[block_idx + 1] = sig_float;
    
    } // for(size_t block_idx
}

/* Initialise global variables */
void initialize_global_variables(void)
{
    g_pedal_up = true;
    
    // We consider that fields of type bool are false when set to 0.
    memset(g_sounds, 0, sizeof(g_sounds));
    for (size_t idx = 0; idx < NB_SOUNDS; idx++)
    {
        g_sounds[idx].key_up = true;
        g_sounds[idx].volume = 0.0;
    }
    memset(g_wav_notes_file_name_list, 0, sizeof(g_wav_notes_file_name_list));
    
    // Special sounds
    memset(g_wav_special_sounds_file_name_list, 0, sizeof(g_wav_special_sounds_file_name_list));
    strcpy(&g_wav_special_sounds_file_name_list[0], "ready.wav");
    strcpy(&g_wav_special_sounds_file_name_list[1 * MAX_FILE_NAME_LEN], "program_charging.wav");
}

/* Read the button controlling the programming mode (Jumper J7) */
bool read_prog_mode_button(void)
{
    bool button_closed;

    GPIO prog_button;

    prog_button.Init(D11, GPIO::Mode::INPUT, GPIO::Pull::NOPULL);

    // Reverse logic
    button_closed = !prog_button.Read();

    return button_closed;
}

/* Mount the SD card */
void mount_sd_card(void)
{
    FRESULT result;
    static SdmmcHandler sd_card;
    static FatFSInterface fsi;

    // Init SD Card
    static SdmmcHandler::Config sd_cfg;
    sd_cfg.Defaults();
    sd_card.Init(sd_cfg);
    
    // Links libdaisy i/o to fatfs driver.
    fsi.Init(FatFSInterface::Config::MEDIA_SD);

    // Mount SD Card
    result = f_mount(&fsi.GetSDFileSystem(), "/", 1);

    if (result != FR_OK)
    {
        g_hw.PrintLine("f_mount result KO");
    }
}

/* Build the full path where the note wav files are located */
void build_notes_wav_file_path(uint8_t prog_idx, char* path_str)
{
    char prog_index_str[3];
    
    // Convert program index into a string
    itoa(prog_idx, prog_index_str, 10);

    // Build the full path
    strcpy(path_str, WAV_NOTES_BASE_FILE_PATH);
    strcat(path_str, "/");
    strcat(path_str, "prog_");
    strcat(path_str, prog_index_str);    
}

/* Build a list of all wav files name in the wav directory of the SD card containing notes. */
void build_notes_wav_notes_file_name_list(uint8_t prog_idx)
{
    DIR     dir;
    FRESULT result;
    FILINFO finf;
    size_t file_index;
    char search_path[MAX_FILE_PATH_LEN];
    char index_str[4];
    uint16_t nb_wav_files = 0;

    memset(index_str, 0, sizeof(index_str));

    // Build the search path
    build_notes_wav_file_path(prog_idx, search_path);
    g_hw.PrintLine("search_path=%s", search_path);

    // Open the directory containing the wav files.
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
        
        // Check if its a wav file. If yes, add it to the list.
        g_hw.PrintLine("finf.fname=%s", finf.fname);

        if(strstr(finf.fname, ".wav") || strstr(finf.fname, ".WAV"))
        {
            g_hw.PrintLine("Wav file found:%s", finf.fname);
            
            // Compute the file index based on the 3 first characters of the file name
            strncpy(&index_str[0], &finf.fname[0], 3);
            file_index = atoi(index_str) - 1;
            
            // Copy the file name at the right file index
            strcpy(&g_wav_notes_file_name_list[file_index * MAX_FILE_NAME_LEN], finf.fname);
            
            nb_wav_files++;
            g_hw.PrintLine("nb_wav_files=%ld", nb_wav_files);
        }
        
        if (nb_wav_files >= NB_KEYS)
        {
            g_hw.PrintLine("Maximum number of files reached");
            break;
        }

    } // End while

    for (size_t idx=0; idx < NB_KEYS; idx++)
    {
        g_hw.PrintLine("file_name=%s", &g_wav_notes_file_name_list[idx * MAX_FILE_NAME_LEN]);
    }
    
    f_closedir(&dir);
}

/* Read and load the special wav file data in external RAM. 
   At the beginning of external RAM.
   Update the position where to load the first note in exernal RAM.
   Update the g_sounds array fields first_sample_pos, last_sample_pos, nb_samples... */
void load_special_sounds_wav_files_in_ram(void)
{
    size_t cur_sound_pos = 0;
    TSoundData *pCurSound;
    char file_path_and_name[MAX_FILE_PATH_LEN];
    size_t wav_data_size_bytes;
    uint8_t* ram_address;

    for (uint16_t sound_idx = 0; sound_idx < NB_SPECIAL_SOUNDS; sound_idx++)
    {
        // Current sound data
        pCurSound = &g_sounds[sound_idx];

        // Build the full file path name
        strcpy(file_path_and_name, WAV_SPECIAL_SOUNDS_FILE_PATH);
        strcat(file_path_and_name, "/");
        strcat(file_path_and_name, &g_wav_special_sounds_file_name_list[sound_idx * MAX_FILE_NAME_LEN]);
        g_hw.PrintLine("file_path_and_name=%s", file_path_and_name);

        // Load the wav data at the current sound position.
        ram_address = (uint8_t*)(&g_sample_data[cur_sound_pos]);
        wav_data_size_bytes = read_wav_file(file_path_and_name, ram_address);

        // For each sound record the position of the first sample, last sample and number of samples.
        pCurSound->first_sample_pos = cur_sound_pos;
        pCurSound->nb_samples = wav_data_size_bytes / 2; // bytes to word size
        pCurSound->last_sample_pos = pCurSound->first_sample_pos + pCurSound->nb_samples;
        
        // Initialise some fields with the first sample position.
        pCurSound->cur_playing_pos = pCurSound->first_sample_pos;
        pCurSound->key_up_pos      = pCurSound->first_sample_pos;
        pCurSound->pedal_up_pos    = pCurSound->first_sample_pos;

        g_hw.PrintLine("Special sound start_position=%d nb_samples=%d", pCurSound->first_sample_pos, pCurSound->nb_samples);

        // Compute the next note position.
        cur_sound_pos += pCurSound->nb_samples;
    }
    
    // Update global variable g_first_note_position
    g_first_note_position = cur_sound_pos;
}

/* Read and load the notes wav file data in external RAM. One file per note.
   After special sounds in external RAM.
   Update the sounds array fields first_sample_pos, last_sample_pos, nb_samples... */
void load_notes_wav_files_in_ram(uint8_t prog_idx)
{
    char file_path_and_name[MAX_FILE_PATH_LEN];
    size_t wav_data_size_bytes = 0;
    size_t cur_note_pos = g_first_note_position;
    uint8_t* ram_address = NULL;
    TSoundData *pCurNote;

    for (uint16_t file_idx = 0; file_idx < NB_KEYS; file_idx++)
    {
        // Current note data
        pCurNote = &g_sounds[NB_SPECIAL_SOUNDS + file_idx];

        // Build the full file path
        build_notes_wav_file_path(prog_idx, file_path_and_name);
        strcat(file_path_and_name, "/");
        strcat(file_path_and_name, &g_wav_notes_file_name_list[file_idx * MAX_FILE_NAME_LEN]);
        g_hw.PrintLine("file_path_and_name=%s", file_path_and_name);
        
        // Load the wav data at the current note position.
        ram_address = (uint8_t*)(&g_sample_data[cur_note_pos]);
        wav_data_size_bytes = read_wav_file(file_path_and_name, ram_address);

        // For each note record the position of the first sample, last sample and number of samples.
        pCurNote->first_sample_pos = cur_note_pos;
        pCurNote->nb_samples = wav_data_size_bytes / 2; // bytes to word size
        pCurNote->last_sample_pos = pCurNote->first_sample_pos + pCurNote->nb_samples;
        
        // Initialise some fields with the first sample position.
        pCurNote->cur_playing_pos = pCurNote->first_sample_pos;
        pCurNote->key_up_pos      = pCurNote->first_sample_pos;
        pCurNote->pedal_up_pos    = pCurNote->first_sample_pos;

        g_hw.PrintLine("Note start_position=%d nb_samples=%d", pCurNote->first_sample_pos, pCurNote->nb_samples);

        // Compute the next note position.
        cur_note_pos += pCurNote->nb_samples;
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
    enum UartHandler::Result result;
    uint8_t char_rec = 0;
    
    while(true)
    {
        result = p_uart->BlockingReceive(&char_rec, 1, 1); // Timeout = 1 ms
        if (result == UartHandler::Result::OK)
        {
            g_hw.PrintLine("Flush 1 character");
        } 
        else
        {
            break;
        }
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
    while(true) 
    {   
        uart_result = p_uart->BlockingReceive(&char_rec, 1, 0);
        if ((uart_result == UartHandler::Result::OK) && (char_rec == 'S'))
        {
            break;
        }
    }

    // Receive characters until end of message (character 0x0a).
    while(true)
    {
        uart_result = p_uart->BlockingReceive(&char_rec, 1, 1000); // Timeout = 1 sec
        if (uart_result == UartHandler::Result::OK)
        {
            msg_rec[char_idx] = char_rec;
            if (char_idx < MAX_MESSAGE_SIZE)
            {
                char_idx++;
            }
            else
            {
                g_hw.PrintLine("Error: Message received too long");
                break;
            }
        } 
        else
        {
            g_hw.PrintLine("Error during message reception. uart_result=%d", uart_result);
            break;
        }

        if (char_rec == 0x0a)
        {
            break;
        } 
    
    } // while(true)
   
    if ( (char_idx >= MAX_MESSAGE_SIZE) || (uart_result != UartHandler::Result::OK)) 
    {
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
            g_hw.PrintLine("Error: Problem to convert key_index received");
            result = -1;
        }
        *p_key_index = arduino_to_piano_key_index(key_index);
        
        // Attack time
        strncpy(temp_str, &msg_rec[4], msg_len - 4);
        temp_str[msg_len] = 0;
        result_2 = sscanf(temp_str, "%ld", &time);
        if (result_2 != 1)
        {
            g_hw.PrintLine("Error: Problem to convert time received");
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
            g_hw.PrintLine("Error: Problem to convert key_index received");
            result = -1;
        }
        *p_key_index = arduino_to_piano_key_index(key_index);
    }
    else
    {
        g_hw.PrintLine("Error: Unknown message received");
        result = -1;
    }

    return result;
}

/* This function manages the messages received (KEY_UP_MSG, KEY_DOWN_MSG) in normal 
   mode (aka not programming mode).
   It works in collaboration with function AudioCallback which is called in parallel. 
   Be careful, as AudioCallback is called asynchronously, the order in which variables 
   are set can matter.
*/
void manage_msg_received_in_normal_mode(uint16_t key_index, e_msg_type msg_type, uint32_t attack_time)
{
    TSoundData *pCurNote;
    uint16_t idx;
    
    if (key_index != PEDAL_KEY_IDX)
    {
        // A key from the keyboard has changed state.
        pCurNote = &g_sounds[NB_SPECIAL_SOUNDS + key_index];
        
        if (msg_type == KEY_DOWN_MSG) 
        {
            // The key is down
            pCurNote->volume = compute_volume(attack_time);

            #if (ENABLED_ALL_LOGS == 1)
                g_hw.Print("KEY_DOWN index=%d attack_time=%ld", key_index, attack_time);
                g_hw.PrintLine(" volume="FLT_FMT3, FLT_VAR3(pCurNote->volume));
            #endif

            pCurNote->cur_playing_pos = pCurNote->first_sample_pos;
            pCurNote->key_up_pos      = pCurNote->first_sample_pos;
            pCurNote->pedal_up_pos    = pCurNote->first_sample_pos;
            pCurNote->key_up          = false;
            pCurNote->sound_end_soon  = false;

            // Start the note playing by the AudioCallback function.
            pCurNote->playing = true;

        } 
        else if (msg_type == KEY_UP_MSG) 
        {
            // The key is up
            #if (ENABLED_ALL_LOGS == 1)
                g_hw.PrintLine("KEY_UP index=%d", key_index);
            #endif
           
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
            g_hw.PrintLine("PEDAL_DOWN");
            g_pedal_up = false;
            
            // The position is reinitialised for all notes (different position for ech note).
            for (idx = 0; idx < NB_KEYS; idx++)
            {
                g_sounds[NB_SPECIAL_SOUNDS + idx].pedal_up_pos = g_sounds[NB_SPECIAL_SOUNDS + idx].first_sample_pos;
            }
        } 
        else if (msg_type == KEY_UP_MSG) 
        {
            // The pedal is up
            g_hw.PrintLine("PEDAL_UP");

            // The position when the pedal is up is set for all notes (different positions for 
            // each note).
            for (idx = 0; idx < NB_KEYS; idx++)
            {
                g_sounds[NB_SPECIAL_SOUNDS + idx].pedal_up_pos = g_sounds[NB_SPECIAL_SOUNDS + idx].cur_playing_pos;
            }
            g_pedal_up = true;
        }
    }
}

/* This function manages the messages received (KEY_DOWN_MSG) in programming mode.
   It works in collaboration with function AudioCallback which is called in parallel. 
   Be careful, as AudioCallback is called asynchronously, the order in which variables 
   are set can matter.
*/
void manage_msg_received_in_programming_mode(uint16_t key_index, e_msg_type msg_type)
{
    uint8_t prog_index;

    if (msg_type == KEY_UP_MSG) 
    {
        // Play sound "Program charging"
        g_hw.PrintLine("Play the sound program charging...");
        play_special_sound(SOUND_PROGRAM_CHARGING_IDX);

        // Compute program index and save it on SD card
        g_hw.PrintLine("Programming mode");
        prog_index = (key_index % 2) + 1;
        g_hw.PrintLine("Program index selected=%d", prog_index);

        g_hw.PrintLine("Writing program index on SD card...");
        write_current_program(prog_index);

        // Build a sorted list of wav file name (one per note).
        g_hw.PrintLine("Building the list of wav files...");
        toggle_right_led();
        build_notes_wav_notes_file_name_list(prog_index);

        // Read notes wav files and load them in RAM.
        g_hw.PrintLine("Loading notes wav files in RAM...");
        toggle_right_led();
        load_notes_wav_files_in_ram(prog_index);

        // Play sound "Piano ready"
        g_hw.PrintLine("Play the sound piano ready...");
        play_special_sound(SOUND_READY_IDX);
    }
}

/* Play a special sound */
void play_special_sound(uint8_t sound_idx)
{
    TSoundData *pCurSound = &g_sounds[sound_idx];

    pCurSound->volume          = 1.0f;
    pCurSound->cur_playing_pos = pCurSound->first_sample_pos;
    pCurSound->key_up_pos      = pCurSound->first_sample_pos;
    pCurSound->pedal_up_pos    = pCurSound->first_sample_pos;
    pCurSound->key_up          = false;
    pCurSound->sound_end_soon  = false;

    // Start the sound playing by the AudioCallback function.
    pCurSound->playing = true;
}

/* This function manages the messages received (KEY_UP_MSG, KEY_DOWN_MSG).
   It works in collaboration with function AudioCallback which is called in parallel. 
   Be careful, as AudioCallback is called asynchronously, the order in which variables 
   are set can matter.
*/
void manage_msg_received(uint16_t key_index, e_msg_type msg_type, uint32_t attack_time)
{
    toggle_right_led();

    bool programming_mode = read_prog_mode_button();

    if (programming_mode)
    {
        manage_msg_received_in_programming_mode(key_index, msg_type);
    }
    else
    {
        manage_msg_received_in_normal_mode(key_index, msg_type, attack_time);
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
            g_hw.PrintLine("f_read result KO. result=%d", result);
        }
        f_close(&SDFile);
    }
    else
    {
        g_hw.PrintLine("f_open result KO. result=%d", result);
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
            g_hw.PrintLine("f_read result KO. result=%d", result);
        }
    
        f_close(&SDFile);

        wav_data_size = bytesRead;
    }
    else
    {
        g_hw.PrintLine("f_open result KO. result=%d", result);
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

    g_hw.SetLed(led_state);
}

/* Display data of a note. Useful for debugging.
   Positions are displayed relatively to the first position.*/
void display_sound_data(uint16_t idx) 
{
    size_t first_pos = g_sounds[idx].first_sample_pos;

    g_hw.Print("idx=%d ", idx);
    g_hw.Print("playing=%d ", g_sounds[idx].playing);
    g_hw.Print("key_up=%d ", g_sounds[idx].key_up);
    // g_hw.Print("first_pos=%d ", g_sounds[idx].first_sample_pos);
    g_hw.Print("last_pos=%d ", g_sounds[idx].last_sample_pos - first_pos);
    g_hw.Print("cur_pos=%d ", g_sounds[idx].cur_playing_pos - first_pos);
    g_hw.Print("kup_pos=%d ", g_sounds[idx].key_up_pos - first_pos);
    g_hw.PrintLine("pup_pos=%d", g_sounds[idx].pedal_up_pos - first_pos);
}

/* Display data of all sounds. Useful for debugging.
   Positions are displayed relatively to the first position.*/
void display_all_sounds_data(void)
{
    for( uint16_t idx=0; idx<NB_SOUNDS; idx++)
    {
        display_sound_data(idx);
    }
}

/* Update the file defining the current program index. */
void write_current_program(uint8_t prog_idx)
{   
    static FIL SDFile;
    FRESULT result;
    static UINT nb_bytes_written;
    static uint8_t byte_to_write = prog_idx;
    
    result = f_open(&SDFile, CURRENT_PROG_FILE_PATH, (FA_WRITE) | (FA_CREATE_ALWAYS));

    if (result == FR_OK)
    {
        result = f_write(&SDFile, &byte_to_write, 1, &nb_bytes_written);
        if ((result != FR_OK) || (nb_bytes_written != 1))
        {
            g_hw.PrintLine("f_write result KO. result=%d nb_bytes_written=%d", result, nb_bytes_written);
        } 
        
        result = f_sync(&SDFile);
        if (result != FR_OK)
        {
            g_hw.PrintLine("f_sync result KO. result=%d", result);
        }

        result = f_close(&SDFile);
        if (result != FR_OK)
        {
            g_hw.PrintLine("f_close result KO. result=%d", result);
        }
    }
    else
    {
        g_hw.PrintLine("f_open result KO. result=%d", result);
    }
}

/* Read the file defining the current program index. */
uint8_t read_current_program(void)
{
    static FIL SDFile;
    FRESULT result;
    uint8_t cur_prog_idx = 1;
    static uint8_t byte_read;
    static UINT nb_bytes_read;

    result = f_open(&SDFile, CURRENT_PROG_FILE_PATH, FA_READ);

    if (result == FR_OK)
    {
        result = f_read(&SDFile, &byte_read, 1, &nb_bytes_read);

        if ((result == FR_OK) && (nb_bytes_read == 1))
        {
            cur_prog_idx = byte_read;
        } 
        else
        {
            g_hw.PrintLine("f_read result KO. result=%d nb_bytes_read=%d", result, nb_bytes_read);    
        }

        result = f_close(&SDFile);
        if (result != FR_OK)
        {
            g_hw.PrintLine("f_close result KO. result=%d", result);
        }
    }
    else
    {
        g_hw.PrintLine("f_open result KO. result=%d", result);
    }

    return cur_prog_idx;
}

/* Main program */
int main(void)
{
    static UartHandler uart;
    uint8_t cur_prog_idx;

    // Initialise global variables
    initialize_global_variables();

    // Initialise hardware
    g_hw.Init();
    toggle_right_led();

    // Initialise serial log.
    // Set parameter to true to wait for the serial line connection.
    g_hw.StartLog(WAIT_UART_HOST_CONNECTION_TO_START == 1);
    toggle_right_led();

    // Initialise and mount the SD card
    g_hw.PrintLine("Mounting SD card...");
    toggle_right_led();
    mount_sd_card();

    // Read current prog
    g_hw.PrintLine("Read current prog...");
    cur_prog_idx = read_current_program();
    g_hw.PrintLine("cur_prog_idx=%d", cur_prog_idx);

    // Read special wav files and load them in RAM.
    g_hw.PrintLine("Loading special wav files in RAM...");
    toggle_right_led();
    load_special_sounds_wav_files_in_ram();

    // Build a sorted list of wav file name (one per note).
    g_hw.PrintLine("Building the list of wav files...");
    toggle_right_led();
    build_notes_wav_notes_file_name_list(cur_prog_idx);

    // Read notes wav files and load them in RAM.
    g_hw.PrintLine("Loading notes wav files in RAM...");
    toggle_right_led();
    load_notes_wav_files_in_ram(cur_prog_idx);

    // Initialize UART
    g_hw.PrintLine("Initializing UART...");
    toggle_right_led();
    initialize_uart(&uart);
    flush_uart(&uart);

	// Prepare and start the audio call back
    g_hw.PrintLine("Preparing and starting audio call back...");
    toggle_right_led();
    g_hw.SetAudioBlockSize(4); // number of samples handled per callback
	g_hw.SetAudioSampleRate(SaiHandle::Config::SampleRate::SAI_48KHZ);
	g_hw.StartAudio(AudioCallback);
    
    // Play the sound "Piano ready".
    g_hw.PrintLine("Play the sound piano ready...");
    toggle_right_led();
    play_special_sound(SOUND_READY_IDX);

    // Play notes received from arduino.
    g_hw.PrintLine("Playing notes received from arduino...");
    toggle_right_led();
    play_notes_received_from_arduino(&uart);

} // int main(void)
