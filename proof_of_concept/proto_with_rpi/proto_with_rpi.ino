/* Program that sends info about the key pressed to Raspberry PI through USB*/

//**** Constants ****
#define OUTPUT_ENABLE 14

#define ADDRESS_0 16
#define ADDRESS_1 17
#define ADDRESS_2 18

#define SW_COM  2
#define SW_DOWN 3

#define KEY_UP    0b11
#define KEY_FLOAT 0b01
#define KEY_DOWN  0b00

#define NB_KEYS 6

//**** Variables ****
unsigned char prev_key_state[NB_KEYS];
unsigned char monitor_state[NB_KEYS];
unsigned long time_start_up_float[NB_KEYS];
unsigned long delta_time_up_float_down[NB_KEYS];
unsigned char prev_display_key_state[NB_KEYS];

void initialise_all_keys(void)
{
  for (unsigned char key_index=0; key_index < NB_KEYS; key_index++)
  {
    prev_key_state[key_index] = KEY_UP;
    monitor_state[key_index] = 0;
    time_start_up_float[key_index] = 0;
    delta_time_up_float_down[key_index] = 0;
    prev_display_key_state[key_index] = KEY_UP;
  }
}

void setup(void) 
{
  // Configure serial com (USB to serial)
  Serial.begin(115200);

  // Configure input
  pinMode(SW_COM, INPUT);
  pinMode(SW_DOWN, INPUT);

  // Configure output
  pinMode(OUTPUT_ENABLE, OUTPUT);
  pinMode(ADDRESS_0, OUTPUT);
  pinMode(ADDRESS_1, OUTPUT);
  pinMode(ADDRESS_2, OUTPUT);

  // Enable multiplexers
  digitalWrite(OUTPUT_ENABLE, LOW);

  initialise_all_keys();
}

/* Read the state of a key */
unsigned char readKeyState(unsigned char keyIndex)
{
  // Select address
  digitalWrite(ADDRESS_0, keyIndex & 0x01u);
  digitalWrite(ADDRESS_1, (keyIndex & 0x02u) >> 1);
  digitalWrite(ADDRESS_2, (keyIndex & 0x04u) >> 2);

  // Read value
  unsigned char com_val = digitalRead(SW_COM);
  unsigned char down_val = digitalRead(SW_DOWN);

  return (com_val << 1) | down_val;
}

void loop(void) 
{
  for (unsigned char key_index=0; key_index < NB_KEYS; key_index++)
  {
    manage_key(key_index);
  }
}

void manage_key(unsigned char key_index) 
{
  unsigned char cur_key_state;

  cur_key_state = readKeyState(key_index);

  if (cur_key_state != prev_key_state[key_index]) 
  {
    if ((prev_key_state[key_index] == KEY_UP) && (cur_key_state == KEY_FLOAT))
    {
      // UP -> FLOAT transition
      if (monitor_state[key_index] == 0)
      {
        monitor_state[key_index] = 1;
        time_start_up_float[key_index] = micros();
      }
    } 
    else if ((prev_key_state[key_index] == KEY_FLOAT) && (cur_key_state == KEY_UP))
    {
      // FLOAT -> UP transition
      if (monitor_state[key_index] == 3)
      {
        Serial.print("U ");
        Serial.print(key_index);
        Serial.println();
      }
      monitor_state[key_index] = 0;
      delta_time_up_float_down[key_index] = 0;
    } 
    else if ((prev_key_state[key_index] == KEY_FLOAT) && (cur_key_state == KEY_DOWN))
    {
      // FLOAT -> DOWN transition
      if (monitor_state[key_index] == 1)
      {
        monitor_state[key_index] = 2;
        
        delta_time_up_float_down[key_index] = micros() - time_start_up_float[key_index];

        Serial.print("D ");
        Serial.print(key_index);
        Serial.print(" ");
        Serial.print(delta_time_up_float_down[key_index]);
        Serial.println();
      }
    } 
    else if ((prev_key_state[key_index] == KEY_DOWN) && (cur_key_state == KEY_FLOAT))
    {
      // DOWN -> FLOAT transition
      if (monitor_state[key_index] == 2)
      {
        monitor_state[key_index] = 3;
      }
    } 
  }

  prev_key_state[key_index] = cur_key_state; 
}