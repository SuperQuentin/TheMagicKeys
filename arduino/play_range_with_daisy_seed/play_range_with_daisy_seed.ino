/************************************************************************************************** 
Play all the notes (a.k.a the range ) using the daisy seed. 
The key down time, key up time and attack time vary between each range.
Info about the key played are sent to the Daisy Seed through serial line (and USB for
debug). 
**************************************************************************************************/

/**** Constants ****/
#define NB_KEYS 85

/* Function called at startup */
void setup(void) 
{
  // Configure serial com (USB to serial and serial line)
  Serial.begin(115200);
  Serial1.begin(115200);

  // Switch on the onboard LED
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH);
}

/* Function called regularly after setup */
void loop(void) 
{
  unsigned long key_time_ms = 32; // Key up and down time.
  unsigned long attack_time = 256;
  bool key_time_going_down = false;
  bool attack_time_going_down = false;

  do
  {
    // Play the range
    for(unsigned char key_index=0; key_index<NB_KEYS; key_index++)
    {
      send_key_down_msg(key_index, attack_time);

      delay(key_time_ms);

      send_key_up_msg(key_index);

      delay(key_time_ms);
    }

    // Change of Key up and down time.
    if (key_time_going_down == true)
    {
        key_time_ms /= 2;
        if (key_time_ms <= 32)
        {
            key_time_going_down = false;
        }
    } 
    else
    {
        key_time_ms *= 2;
        if (key_time_ms >= 16384)
        {
            key_time_going_down = true;
        }
    }

    // Change attack time.
    if (attack_time_going_down == true)
    {
        attack_time /= 2;
        if (attack_time <= 256)
        {
            attack_time_going_down = false;
        }
    } 
    else
    {
        attack_time *= 2;
        if (attack_time >= 8192)
        {
            attack_time_going_down = true;
        }
    }

  } while(true);
}

/* Send the KEY_DOWN message on the UART Serial and Serial1 */
void send_key_down_msg(unsigned char key_index, unsigned long time)
{
        Serial.print("SD ");
        Serial.print(key_index);
        Serial.print(" ");
        Serial.print(time);
        Serial.println();

        Serial1.print("SD ");
        Serial1.print(key_index);
        Serial1.print(" ");
        Serial1.print(time);
        Serial1.println();
}

/* Send the KEY_UP message on the UART Serial and Serial1 */
void send_key_up_msg(unsigned char key_index)
{
        Serial.print("SU ");
        Serial.print(key_index);
        Serial.println();

        Serial1.print("SU ");
        Serial1.print(key_index);
        Serial1.println();
}
