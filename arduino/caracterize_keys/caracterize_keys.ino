/* Program to caracterize a piano key (bouncing, timings...)*/

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

//**** Variables ****
int prev_key_state = KEY_UP;
int monitor_state = 0;
int nb_up_float_transition = 0;
int nb_float_down_transition = 0;
int nb_unexpected_transition = 0;
unsigned long time_start_up_float = 0;
unsigned long time_start_float_down = 0;
unsigned long time_start_down_float = 0;
unsigned long delta_time_up_float_down = 0;
unsigned long delta_time_float_down_float = 0;
unsigned long delta_time_down_float_up = 0;
int prev_display_key_state = KEY_UP;

void setup() 
{
  // Configure serial com
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
}

/* Read the state of a key */
int readKeyState(unsigned char keyIndex)
{
  // Select address
  digitalWrite(ADDRESS_0, keyIndex & 0x01u);
  digitalWrite(ADDRESS_1, (keyIndex & 0x02u) >> 1);
  digitalWrite(ADDRESS_2, (keyIndex & 0x04u) >> 2);

  // Read value
  int com_val = digitalRead(SW_COM);
  int down_val = digitalRead(SW_DOWN);

  return (com_val << 1) | down_val;
}

void loop() 
{
  int cur_key_state;
  unsigned long cur_time;

  cur_key_state = readKeyState(5);

  if (cur_key_state != prev_key_state) 
  {
    if ((prev_key_state == KEY_UP) && (cur_key_state == KEY_FLOAT))
    {
      // UP -> FLOAT transition
      if (monitor_state == 0)
      {
        monitor_state = 1;
        nb_up_float_transition = 0;
        time_start_up_float = micros();
      }
      nb_up_float_transition++;
      
    } else if ((prev_key_state == KEY_FLOAT) && (cur_key_state == KEY_UP))
    {
      // FLOAT -> UP transition
      if (monitor_state == 3)
      {
        monitor_state = 4;
        cur_time = micros();
        delta_time_down_float_up = cur_time - time_start_down_float;
      }

      nb_up_float_transition++;

    } else if ((prev_key_state == KEY_FLOAT) && (cur_key_state == KEY_DOWN))
    {
      // FLOAT -> DOWN transition
      if (monitor_state == 1)
      {
        monitor_state = 2;
        nb_float_down_transition = 0;
        cur_time = micros();
        time_start_float_down = cur_time;
        delta_time_up_float_down = cur_time - time_start_up_float;
      }
      nb_float_down_transition++;

    } else if ((prev_key_state == KEY_DOWN) && (cur_key_state == KEY_FLOAT))
    {
      // DOWN -> FLOAT transition
      if (monitor_state == 2)
      {
        monitor_state = 3;
        cur_time = micros();
        time_start_down_float = cur_time;
        delta_time_float_down_float = cur_time - time_start_float_down;
      }
      nb_float_down_transition++;
    } else 
    {
       // Transition not expected
      nb_unexpected_transition++;
    }
  }

  prev_key_state = cur_key_state; 

  // Use key 1 to send information collected about key 0
  cur_key_state = readKeyState(1);
  if ((prev_display_key_state == KEY_FLOAT) && (cur_key_state == KEY_DOWN))
  {
    // Send info about key 0
    Serial.print("nb_up_float_transition = ");
    Serial.print(nb_up_float_transition);
    Serial.println();
    Serial.print("nb_float_down_transition = ");
    Serial.print(nb_float_down_transition);
    Serial.println();
    Serial.print("nb_unexpected_transition = ");
    Serial.print(nb_unexpected_transition);
    Serial.println();
    Serial.print("delta_time_up_float_down = ");
    Serial.print(delta_time_up_float_down);
    Serial.println();
    Serial.print("delta_time_float_down_float = ");
    Serial.print(delta_time_float_down_float);
    Serial.println();
    Serial.print("delta_time_down_float_up = ");
    Serial.print(delta_time_down_float_up);
    Serial.println();

    Serial.println();

    // Reset info about key 0
    monitor_state = 0;
    nb_up_float_transition = 0;
    nb_float_down_transition = 0;
    nb_unexpected_transition = 0;
    delta_time_up_float_down = 0;
    delta_time_float_down_float = 0;
    delta_time_down_float_up = 0;
  }
  
  prev_display_key_state = cur_key_state; 
}
