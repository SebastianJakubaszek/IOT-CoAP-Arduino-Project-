

#include <RF24Network.h>
#include <RF24.h>
#include <SPI.h>

RF24 radio(7,8);                    
unsigned long licznik = 0;
RF24Network network(radio);        
bool enter = false;
bool notenter = false;

struct payload_t {         
  int hour;
  int mins;
  int secs;
  
};
struct message{
  bool on;
  bool off;
  bool getStatus;
};
struct lampka_t{
  bool stan;
}; 
  lampka_t lampka;
void setup(void)
{
  Serial.begin(115200);
  lampka.stan = false;
  SPI.begin();
  radio.begin();
  network.begin(105,01);
  pinMode(2, INPUT);
  pinMode(5, OUTPUT);
  digitalWrite(5,255);

}

  payload_t czas = {0,0,0};
  unsigned long temp = 0;
void loop() {                      
  unsigned long now = millis(); 
  if (now - temp > 1000){
    czas.secs+=1;
    temp = now;
  }
  if(czas.secs >= 60){
    czas.secs = 0;
    czas.mins+=1;
  }
  if(czas.mins >= 60){
    czas.mins = 0;
    czas.hour+=1;
  }
  
  if ( digitalRead(2) == LOW  ){
    enter = true;
  }
  else if (digitalRead(2)==HIGH){
    notenter = true;
    enter = false;
  }
  if(enter)
    if(notenter)
  {
    RF24NetworkHeader header(00);
    bool ok = network.write(header,&czas,sizeof(czas));
    if (ok)
      Serial.println("wyslane");
    else
      Serial.println("niewyslane");
      licznik++;
      enter = false;
      notenter = false;
  }
  network.update();   
  while(network.available()){
    RF24NetworkHeader header1;
    message payload;
    network.read(header1, &payload, sizeof(payload));
    if (payload.on){
      digitalWrite(5,0);
      lampka.stan = true;
    }
    else if (payload.off){
      lampka.stan = false;
      digitalWrite(5,255);
    }
    else if (payload.getStatus){
          RF24NetworkHeader header(00);
          Serial.println(lampka.stan);
          if(network.write(header,&lampka,sizeof(lampka)))
            Serial.println("Wyslano");
          else
            Serial.println("Nie wyslano");
    }
      
  }
  
}
