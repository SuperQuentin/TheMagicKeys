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

void setup() {
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

void loop() {
  unsigned char com_val;
  unsigned char down_val;
  unsigned char key_state;
  unsigned char keyIndex = 0;
 
  // Select the first group of boards (sat. board 1..7)   
  digitalWrite(ENABLE_GROUP_1_BAR, LOW);
  digitalWrite(ENABLE_GROUP_2_BAR, HIGH);

  // Select a key
  digitalWrite(KEY_ADDRESS_0, keyIndex & 0x01u);
  digitalWrite(KEY_ADDRESS_1, (keyIndex & 0x02u) >> 1);
  digitalWrite(KEY_ADDRESS_2, (keyIndex & 0x04u) >> 2);

  // Read key state for 1 board
  com_val = digitalRead(SAT_BOARD_4_OR_11_SW_COM);
  down_val = digitalRead(SAT_BOARD_4_OR_11_SW_DOWN);
  key_state = (com_val << 1) | down_val;

  if (key_state == KEY_UP)
  { 
    Serial.print("KEY_UP");
  } 
  else if (key_state == KEY_DOWN)
  {
    Serial.print("KEY_DOWN");
  } 
  else if (key_state == KEY_FLOAT)
  {
    Serial.print("KEY_FLOAT");
  } 
  else 
  {
    Serial.print("UNKNOWN_STATE");
  }
  Serial.println("");

  delay(200);
}
