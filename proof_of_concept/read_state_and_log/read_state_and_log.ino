#define OUTPUT_ENABLE 14

#define ADDRESS_0 16
#define ADDRESS_1 17
#define ADDRESS_2 18

#define SW_COM 2
#define SW_DOWN 3

void setup() {
  Serial.begin(115200);

  //in
  pinMode(SW_COM, INPUT);
  pinMode(SW_DOWN, INPUT);

  //out
  pinMode(OUTPUT_ENABLE, OUTPUT);
  //pinMode(15, OUTPUT);
  pinMode(ADDRESS_0, OUTPUT);
  pinMode(ADDRESS_1, OUTPUT);
  pinMode(ADDRESS_2, OUTPUT);
}

 

#define KEY_UP 0b11
#define KEY_FLOAT 0b01
#define KEY_DOWN 0b00

// the loop function runs over and over again forever
void loop() {
  //enable multiplexers
  digitalWrite(OUTPUT_ENABLE, LOW);

  Serial.println("");

  for(int i  = 0; i<6; i++){
    int val = readKey(i);
    Serial.print(val == KEY_UP ? " UP " : val == KEY_FLOAT ? "MID " : val == KEY_DOWN ? "DOWN" : "????");
    //Serial.print(val, BIN);
    Serial.print(" ");
  }

  //delay(50);
}

//keyindex 0...5
int readKey(int keyIndex){
  //set address
  digitalWrite(ADDRESS_0, keyIndex & 0b001);
  digitalWrite(ADDRESS_1, keyIndex & 0b010);
  digitalWrite(ADDRESS_2, keyIndex & 0b100);

  //read
  int com_val = digitalRead(SW_COM);
  int down_val = digitalRead(SW_DOWN);

  return com_val * 2 + down_val;
}