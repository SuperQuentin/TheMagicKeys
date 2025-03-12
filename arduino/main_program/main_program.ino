/************************************************************************************************** 
Main Arduino Program that sends info about the key to Daisy Seed through serial line (and USB for
debug). Data are sent only when the state of a key change.
**************************************************************************************************/

/**** Constants ****/
#define NB_SAT_BOARDS         14
#define NB_KEY_PER_SAT_BOARD  7
#define NB_KEYS               (NB_SAT_BOARDS * NB_KEY_PER_SAT_BOARD)

// Outputs to enable group 1 (sat. board 1..7) or group 2 (sat. board 8..14). Inverse logic.
#define ENABLE_GROUP_1_BAR 14 // A0 
#define ENABLE_GROUP_2_BAR 15 // A1

// Outputs to address a key on all sat. board of a group (7 keys selected at once) 
#define KEY_ADDRESS_0 16 // A2
#define KEY_ADDRESS_1 17 // A3
#define KEY_ADDRESS_2 18 // A4

// 14 inputs to read the state of the 7 keys selected
#define SAT_BOARD_1_OR_8_SW_COM    2  // D2
#define SAT_BOARD_1_OR_8_SW_DOWN   3  // D3
#define SAT_BOARD_2_OR_9_SW_COM    4  // D4
#define SAT_BOARD_2_OR_9_SW_DOWN   5  // D5
#define SAT_BOARD_3_OR_10_SW_COM   6  // D6
#define SAT_BOARD_3_OR_10_SW_DOWN  7  // D7
#define SAT_BOARD_4_OR_11_SW_COM   8  // D8
#define SAT_BOARD_4_OR_11_SW_DOWN  9  // D9
#define SAT_BOARD_5_OR_12_SW_COM   10 // D10
#define SAT_BOARD_5_OR_12_SW_DOWN  11 // D11
#define SAT_BOARD_6_OR_13_SW_COM   12 // D12
#define SAT_BOARD_6_OR_13_SW_DOWN  13 // D13
#define SAT_BOARD_7_OR_14_SW_COM   19 // A5
#define SAT_BOARD_7_OR_14_SW_DOWN  20 // A6

#define KEY_UP    0b11
#define KEY_FLOAT 0b01
#define KEY_DOWN  0b00

/**** Variables ****/
unsigned char cur_keys_state[NB_KEYS];
unsigned char prev_keys_state[NB_KEYS];
unsigned char monitor_state[NB_KEYS];
unsigned long time_start_up_float[NB_KEYS];
unsigned long delta_time_up_float_down[NB_KEYS];

/* Function called at startup */
void setup(void) 
{
  // Configure serial com (USB to serial and serial line)
  Serial.begin(115200);
  Serial1.begin(115200);

  // Configure output pins
  pinMode(ENABLE_GROUP_2_BAR, OUTPUT);
  pinMode(ENABLE_GROUP_1_BAR, OUTPUT);
  pinMode(KEY_ADDRESS_0, OUTPUT);
  pinMode(KEY_ADDRESS_1, OUTPUT);
  pinMode(KEY_ADDRESS_2, OUTPUT);

  // Configure input pins
  pinMode(SAT_BOARD_1_OR_8_SW_COM, INPUT);
  pinMode(SAT_BOARD_1_OR_8_SW_DOWN, INPUT);
  pinMode(SAT_BOARD_2_OR_9_SW_COM, INPUT);
  pinMode(SAT_BOARD_2_OR_9_SW_DOWN, INPUT);
  pinMode(SAT_BOARD_3_OR_10_SW_COM, INPUT);
  pinMode(SAT_BOARD_3_OR_10_SW_DOWN, INPUT);
  pinMode(SAT_BOARD_4_OR_11_SW_COM, INPUT);
  pinMode(SAT_BOARD_4_OR_11_SW_DOWN, INPUT);
  pinMode(SAT_BOARD_5_OR_12_SW_COM, INPUT);
  pinMode(SAT_BOARD_5_OR_12_SW_DOWN, INPUT);
  pinMode(SAT_BOARD_6_OR_13_SW_COM, INPUT);
  pinMode(SAT_BOARD_6_OR_13_SW_DOWN, INPUT);
  pinMode(SAT_BOARD_7_OR_14_SW_COM, INPUT);
  pinMode(SAT_BOARD_7_OR_14_SW_DOWN, INPUT);

  // Initialize multiplexers
  digitalWrite(ENABLE_GROUP_1_BAR, LOW);
  digitalWrite(KEY_ADDRESS_0, LOW); 
  digitalWrite(KEY_ADDRESS_1, LOW);
  digitalWrite(KEY_ADDRESS_2, LOW); 
  digitalWrite(ENABLE_GROUP_1_BAR, HIGH);

  digitalWrite(ENABLE_GROUP_2_BAR, LOW);
  digitalWrite(KEY_ADDRESS_0, LOW); 
  digitalWrite(KEY_ADDRESS_1, LOW);
  digitalWrite(KEY_ADDRESS_2, LOW); 
  digitalWrite(ENABLE_GROUP_2_BAR, HIGH);
  
  // Iniatialize key management data
  initialise_all_keys();

  // Switch on the onboard LED
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH);
}

/* Function called regularly after setup */
void loop(void) 
{
  read_state_of_all_keys(cur_keys_state);

  for (unsigned char key_index=0; key_index < NB_KEYS; key_index++)
  {
    manage_key(key_index);
  }
}

/* Initialise all keys variables */
void initialise_all_keys(void)
{
  for (unsigned char key_index=0; key_index < NB_KEYS; key_index++)
  {
    // FIXME JPM: Initialise with KEY_UP as soon as real keys are connected.
    prev_keys_state[key_index] = KEY_FLOAT;
    monitor_state[key_index] = 0;
    time_start_up_float[key_index] = 0;
    delta_time_up_float_down[key_index] = 0;
  }
}

/* Read the state of all keys */
void read_state_of_all_keys(unsigned char all_keys_state[NB_KEYS]) 
{
  unsigned char key_state_7_boards[7];
  unsigned char all_key_state_idx;

  // Read key states
  for (unsigned char board_group_idx = 0; board_group_idx < 2; board_group_idx++)
  {
    // Group by group
    if (board_group_idx == 0)
    {
      // Select the first group of boards (sat. board 1..7)   
      digitalWrite(ENABLE_GROUP_1_BAR, LOW);
      digitalWrite(ENABLE_GROUP_2_BAR, HIGH);
    } 
    else
    {
      // Select the second group of boards (sat. board 8..14)   
      digitalWrite(ENABLE_GROUP_1_BAR, HIGH);
      digitalWrite(ENABLE_GROUP_2_BAR, LOW);
    } 

    // Key by key
    for (unsigned char key_idx = 0; key_idx < NB_KEY_PER_SAT_BOARD; key_idx++)
    {
      // For 7 boards at once
      read_key_state_on_7_boards(key_idx, key_state_7_boards);

      // Copy each state at the right place in array all_keys_state (ordered by key_index).
      for (unsigned char board_idx = 0; board_idx < 7; board_idx++)
      {
        all_key_state_idx = ((board_group_idx * NB_KEYS) / 2) + (board_idx * NB_KEY_PER_SAT_BOARD) + key_idx; 
        all_keys_state[all_key_state_idx] = key_state_7_boards[board_idx];
      }
    }
  }
}

/* Read the state of a key with a given index on 7 boards */
void read_key_state_on_7_boards(unsigned char keyIndex, unsigned char keys_state[7])
{
  unsigned char com_val;
  unsigned char down_val;

  // Select address
  digitalWrite(KEY_ADDRESS_0, keyIndex & 0x01u);
  digitalWrite(KEY_ADDRESS_1, (keyIndex & 0x02u) >> 1);
  digitalWrite(KEY_ADDRESS_2, (keyIndex & 0x04u) >> 2);

  // Read value
  com_val = digitalRead(SAT_BOARD_1_OR_8_SW_COM);
  down_val = digitalRead(SAT_BOARD_1_OR_8_SW_DOWN);
  keys_state[0] = (com_val << 1) | down_val;

  com_val = digitalRead(SAT_BOARD_2_OR_9_SW_COM);
  down_val = digitalRead(SAT_BOARD_2_OR_9_SW_DOWN);
  keys_state[1] = (com_val << 1) | down_val;

  com_val = digitalRead(SAT_BOARD_3_OR_10_SW_COM);
  down_val = digitalRead(SAT_BOARD_3_OR_10_SW_DOWN);
  keys_state[2] = (com_val << 1) | down_val;

  com_val = digitalRead(SAT_BOARD_4_OR_11_SW_COM);
  down_val = digitalRead(SAT_BOARD_4_OR_11_SW_DOWN);
  keys_state[3] = (com_val << 1) | down_val;

  com_val = digitalRead(SAT_BOARD_5_OR_12_SW_COM);
  down_val = digitalRead(SAT_BOARD_5_OR_12_SW_DOWN);
  keys_state[4] = (com_val << 1) | down_val;

  com_val = digitalRead(SAT_BOARD_6_OR_13_SW_COM);
  down_val = digitalRead(SAT_BOARD_6_OR_13_SW_DOWN);
  keys_state[5] = (com_val << 1) | down_val;

  com_val = digitalRead(SAT_BOARD_7_OR_14_SW_COM);
  down_val = digitalRead(SAT_BOARD_7_OR_14_SW_DOWN);
  keys_state[6] = (com_val << 1) | down_val;
}

/* Manage a key */
void manage_key(unsigned char key_index) 
{
  unsigned char cur_key_state;

  cur_key_state = cur_keys_state[key_index];
  
  if (cur_key_state != prev_keys_state[key_index]) 
  {
    if ((prev_keys_state[key_index] == KEY_UP) && (cur_key_state == KEY_FLOAT))
    {
      // UP -> FLOAT transition
      if (monitor_state[key_index] == 0)
      {
        monitor_state[key_index] = 1;
        time_start_up_float[key_index] = micros();
      }
    } 
    else if ((prev_keys_state[key_index] == KEY_FLOAT) && (cur_key_state == KEY_UP))
    {
      // FLOAT -> UP transition
      if (monitor_state[key_index] == 3)
      {
        send_key_up_msg(key_index);
      }
      monitor_state[key_index] = 0;
      delta_time_up_float_down[key_index] = 0;
    } 
    else if ((prev_keys_state[key_index] == KEY_FLOAT) && (cur_key_state == KEY_DOWN))
    {
      // FLOAT -> DOWN transition
      if (monitor_state[key_index] == 1)
      {
        monitor_state[key_index] = 2;
        
        delta_time_up_float_down[key_index] = micros() - time_start_up_float[key_index];

        send_key_down_msg(key_index, delta_time_up_float_down[key_index]);
      }
    } 
    else if ((prev_keys_state[key_index] == KEY_DOWN) && (cur_key_state == KEY_FLOAT))
    {
      // DOWN -> FLOAT transition
      if (monitor_state[key_index] == 2)
      {
        monitor_state[key_index] = 3;
      }
    } 
  }

  prev_keys_state[key_index] = cur_key_state; 
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
