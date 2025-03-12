#define OUTPUT_ENABLE 14

#define ADDRESS_0 16
#define ADDRESS_1 17
#define ADDRESS_2 18

#define SW_COM 2
#define SW_DOWN 3

#define KEY_UP 0b11
#define KEY_FLOAT 0b01
#define KEY_DOWN 0b00

void setup() {
  Serial.begin(115200);

  //in
  pinMode(SW_COM, INPUT);
  pinMode(SW_DOWN, INPUT);

  //out
  pinMode(OUTPUT_ENABLE, OUTPUT);
  pinMode(ADDRESS_0, OUTPUT);
  pinMode(ADDRESS_1, OUTPUT);
  pinMode(ADDRESS_2, OUTPUT);

  // Initialize multiplexers
  digitalWrite(OUTPUT_ENABLE, HIGH);
  digitalWrite(ADDRESS_0, LOW); 
  digitalWrite(ADDRESS_1, LOW);
  digitalWrite(ADDRESS_2, LOW); 
  digitalWrite(OUTPUT_ENABLE, LOW);
}

// the loop function runs over and over again forever
void loop() {

  Serial.println("");

  for(unsigned char i  = 0; i<6; i++){
    int val = readKey(i);
    Serial.print(val == KEY_UP ? " UP " : val == KEY_FLOAT ? "MID " : val == KEY_DOWN ? "DOWN" : "????");
    Serial.print(" ");
  }

}

//keyindex 0...5
int readKey(unsigned char keyIndex){
  //set address
  digitalWrite(ADDRESS_0, keyIndex & 0x01u);
  digitalWrite(ADDRESS_1, (keyIndex & 0x02u) >> 1);
  digitalWrite(ADDRESS_2, (keyIndex & 0x04u) >> 2);

  //read
  int com_val = digitalRead(SW_COM);
  int down_val = digitalRead(SW_DOWN);

  return com_val * 2 + down_val;
}
