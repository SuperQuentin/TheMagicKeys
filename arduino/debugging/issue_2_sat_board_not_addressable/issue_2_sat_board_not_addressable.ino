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

#define NB_SAT_BOARDS         14
#define NB_KEY_PER_SAT_BOARD  7
#define NB_KEYS               (NB_SAT_BOARDS * NB_KEY_PER_SAT_BOARD)

unsigned char keyStateArrayPrev[128];
unsigned char keyStateArrayCur[128];

void setup() {

  memset(keyStateArrayCur, KEY_FLOAT, NB_KEYS);
  memset(keyStateArrayPrev, KEY_FLOAT, NB_KEYS);

  // Configure serial com (USB to serial)
  Serial.begin(115200);

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

void loop() {
  unsigned char key_state;
 
  memcpy(keyStateArrayPrev, keyStateArrayCur, 128);

  read_state_of_all_keys(keyStateArrayCur);
  
  for (unsigned char idx = 0; idx < 128; idx++)
  {
    if (keyStateArrayCur[idx] != keyStateArrayPrev[idx])
    {
      key_state = keyStateArrayCur[idx];
      if (key_state == KEY_UP)
      { 
        Serial.print("U");
      } 
      else if (key_state == KEY_DOWN)
      {
        Serial.print("D");
      } 
      else if (key_state == KEY_FLOAT)
      {
        Serial.print("F");
      } 
      else 
      {
        Serial.print("?");
      }
      Serial.print(idx);
      
      Serial.println("");
    }
  }
}
