#include "daisysp.h"
#include "daisy_seed.h"

using namespace daisysp;
using namespace daisy;

/* Constants */
#define MAX_MESSAGE_SIZE 20
#define NB_OSCILLATORS   35

/* Types */
typedef enum {KEY_UP_MSG, KEY_DOWN_MSG} e_msg_type;

/* Module variables */ 
static DaisySeed  hw;
static Oscillator osc[NB_OSCILLATORS];

/* Audio callback function */
static void AudioCallback(AudioHandle::InterleavingInputBuffer  in,
                          AudioHandle::InterleavingOutputBuffer out,
                          size_t                                size)
{
    float sig;

    for(size_t block_idx = 0; block_idx < size; block_idx += 2)
    {
        sig = 0;
        for(size_t osc_idx=0; osc_idx < NB_OSCILLATORS; osc_idx++)
        {
            sig += osc[osc_idx].Process();
        }

        // left out
        out[block_idx] = sig;

        // right out
        out[block_idx + 1] = sig;
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

int analyze_msg_received(char msg_rec[MAX_MESSAGE_SIZE], uint16_t *p_key_index, e_msg_type *p_msg_type, uint32_t* p_time)
{
    int result = 0;
    int result_2 = 0;
    uint8_t msg_len = strlen(msg_rec) - 2; // Remove the 2 end of message characters (0x0d and 0x0a).
    char temp_str[MAX_MESSAGE_SIZE];
    int temp_int;
    uint32_t time;
    uint16_t key_index;
    
    *p_time = 0;

    if (msg_rec[0] == 'D')
    {
        // Message type
        *p_msg_type = KEY_DOWN_MSG;

        // key_index
        strncpy(temp_str, &msg_rec[2], 2);
        temp_str[2] = 0;
        result_2 = sscanf(temp_str, "%d", &temp_int);
        key_index = temp_int;
        if (result_2 != 1)
        {
            hw.PrintLine("Error: Problem to convert key_index received");
            result = -1;
        }
        *p_key_index = key_index;
        
        // time
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

        // key_index
        strncpy(temp_str, &msg_rec[2], 2);
        temp_str[2] = 0;
        result_2 = sscanf(temp_str, "%d", &temp_int);
        key_index = temp_int;
        if (result_2 != 1)
        {
            hw.PrintLine("Error: Problem to convert key_index received");
            result = -1;
        }
        *p_key_index = key_index;
    }
    else
    {
        hw.PrintLine("Error: Unknown message received");
        result = -1;
    }

    return result;
}

/* Compute the amplification factor which depends on the attack_time
   We compute a linear function such as: 
   - For time = 300 -> amp_factor = 1/6 (for 6 keys)
   - For time = 10000 -> amp_factor = 0.
   Thus, the linear function is: 
   amp_factor = (1/6) . (-1.03e-4.time + 1.03)
*/
float compute_amplification( uint32_t attack_time)
{
    float amp_factor;
    
    amp_factor = -1.03e-4 * (float)attack_time + 1.03;
    
    if (amp_factor > 1.0f)
    {
        amp_factor = 1.0f;
    }
    else if (amp_factor < 0.1f)
    {
    amp_factor = 0.1f;
    }

    amp_factor = amp_factor * 1.0f / 6.0f;
                                       
    return amp_factor;
}

int main(void)
{
    float sample_rate;
    char msg_rec[MAX_MESSAGE_SIZE];
    int result = 0;
    uint16_t key_index;
    e_msg_type msg_type;
    uint32_t attack_time;
    bool led_state = false;

    // Initialize hardware
    hw.Configure();
    hw.Init();
    hw.SetAudioBlockSize(4);

    // Start message logging
    hw.StartLog(false);

    // Set oscillators parameters
    sample_rate = hw.AudioSampleRate();
    for(size_t osc_idx=0; osc_idx < NB_OSCILLATORS; osc_idx++)
    {
        osc[osc_idx].Init(sample_rate);
        osc[osc_idx].SetWaveform(Oscillator::WAVE_SIN);
        osc[osc_idx].SetFreq(100 * (osc_idx + 1));
        osc[osc_idx].SetAmp(0);
    }

    // Start callback
    hw.StartAudio(AudioCallback);

    // Initialize UART
    UartHandler uart;
    initialize_uart(&uart);
    flush_uart(&uart);

    // Toggle the right LED from OFF to ON
    hw.SetLed(!led_state);

    // Receive messages from UART
    float amp_factor;
    while(1)
    {
        result = receive_msg_on_uart(&uart, msg_rec);
        if (result == 0)
        {   
            result = analyze_msg_received(msg_rec, &key_index, &msg_type, &attack_time);
            // Toggle the right LED
            hw.SetLed(!led_state);

            if (result == 0)
            {
                hw.PrintLine("key_index=%d, msg_type=%d, attack_time=%ld", key_index, msg_type, attack_time);

                if (msg_type == KEY_DOWN_MSG) 
                {
                    amp_factor = compute_amplification(attack_time);

                    hw.PrintLine("amp_factor=" FLT_FMT3, FLT_VAR3(amp_factor));

                    osc[key_index % NB_OSCILLATORS].SetAmp(amp_factor);
                } 
                else if (msg_type == KEY_UP_MSG) 
                {
                    osc[key_index % NB_OSCILLATORS].SetAmp(0.0f);
                }
            }
        }
    }
}
