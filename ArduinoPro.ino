#define COAP_OPTION_DELTA(v, n) (v < 13 ? (*n = (0xFF & v)) : (v <= 0xFF + 13 ? (*n = 13) : (*n = 14)))
#include <RF24Network.h>
#include <RF24.h>
#include <SPI.h>
#include <Ethernet.h>
#include <EthernetUdp.h>
#include <time.h>

//==========================================
//Struktura nagłówka

typedef struct {
  uint8_t ver;
  uint8_t messageType;
  uint8_t tokenLength;
  uint8_t code;
  uint8_t messageID[2];
} coapHeader_t;

//==========================================
//struktura Type-Length-Value
typedef struct {
  const uint8_t *value;
  size_t lenght;
} coapTLV_t;


//==========================================
//struktura dla opcji
typedef struct {
  uint8_t optionNumber;
  uint8_t *value;
  size_t lenght;
} coapOptions_t;

//==========================================
//pakiet
typedef struct {
  coapHeader_t header;
  coapTLV_t token;
  uint8_t numberOfOptions;
  coapOptions_t options[16];
  coapTLV_t payload;
} coapPacket_t;


//==========================================
//Struktury dla radiowej komunikacji
struct payload_t {
  int hour;
  int mins;
  int secs;
};

//==========================================
//Struktury dla komunikacji do lampki
struct message {
  bool on;              // wlacz lampke
  bool off;             // wylacz lampke
  bool getStatus;       // odeslij stan lampki
};
//Struktura stanu lampki
struct lampka {
  bool stan;
};
//=============================================
//=====Zmienne globalne z arduino mini=====

//Struktura stempla czasowego
payload_t payloadTime = { 0, 0, 0 };

//Struktura komunikacji z lampką
message payloadm;

//Zapamietuje ostatni stan lampki
lampka stat;

//===========================================================
//struktury coap, udp.....
byte mac[] = { 0x00, 0xaa, 0xbb, 0xcc, 0xde, 0xf4 };
EthernetUDP udp;
const int MAX_BUFFER = 80;
uint8_t packetBuffer[MAX_BUFFER];
short port = 2354;

//Zmienne dla funkcjonalnosci coap
//eTag [t lub f (wlaczona lub wylaczona)]
char eTagLampka;

//Subskrypcja dla stempla czasowego
bool subscribe = false;

//Funkcjonalnosc block2
//Czy znaleziono w pakiecie opcje (od coopera)
bool block2Found = false;
//Numer (0-16) pod ktora kryje sie opcja block2
uint8_t block2Value;

//Komunikacja radiowa z arduino mini
RF24 radio(7, 8);
RF24Network network(radio);

//zmienne dla statystyk
int wyslane;
int odebrane;
int del;
char metryka[50] = {0};

void setup() {
  //Ustala komunikacje z arduino mini
  setupHardware();
  //===========================================================
  //COAP UDP
  //Ustala wolne IP z routerem i otwiera port
  if (Ethernet.begin(mac) == 0) {
    Serial.println("brak");
    while (1);
  }
  for (int i = 0; i < 4; i++)
  {
    Serial.print(Ethernet.localIP()[i], DEC);
    Serial.print(".");
  }
  Serial.println();
  udp.begin(port);

  //Sprawdza poczatkowy stan lampki, zeby zapisac w pamieci podrecznej
  getLightStatus();
  if (stat.stan == true)
    eTagLampka = 't';
  else
    eTagLampka = 'f';


  del = 0;
  wyslane = 0;
  odebrane = 0;
  srand(time(0));
}

//Utworzenie struktur dla pakietu odebranego i odpowiedzi
coapPacket_t reply;
coapPacket_t coapPacket;


void loop() {
  network.update();
  
  //===========================================================
  //COAP UDP
  int packetSize;
  int rc;

  //Ustalanie rozmiaru pakietu (czy cokolwiek przyszlo)
  packetSize = udp.parsePacket();

  //Linijki dla obserwacji
  //Jezeli subskrybujemy (subscribe == true), i pomyslnie odczytamy godzine z arduino mini
  //To serwer wysyla pakiet do coopera

  if (subscribe) {
    bool observeSend = getTimeStamp();
    if (observeSend) {
      getFunc(&coapPacket, "stempelCzasowy", true, false, subscribe);
      Serial.println("sent");
    }
  }

  //Jesli przyszedl pakiet, parsujemy go
  if (packetSize > 0) {
    odebrane += 1;
    udp.read(packetBuffer, sizeof(packetBuffer));


    //Parsowanie naglowka pakietu
    coapPacket.header.ver = (packetBuffer[0] & 0xC0) >> 6;
    coapPacket.header.messageType = (packetBuffer[0] & 0x30) >> 4;
    coapPacket.header.tokenLength = (packetBuffer[0] & 0x0F);
    coapPacket.header.code = packetBuffer[1];
    coapPacket.header.messageID[0] = packetBuffer[2];
    coapPacket.header.messageID[1] = packetBuffer[3];
    /*Serial.print("Wersja: ");
      Serial.println(coapPacket.header.ver);
      Serial.print("Typ wiadomosci: ");
      Serial.println(coapPacket.header.messageType);
      Serial.print("Dlugosc tokena: ");
      Serial.println(coapPacket.header.tokenLength);
      Serial.print("Kod: ");
      Serial.println(coapPacket.header.code);
      Serial.print("MessageID:");
      Serial.print(coapPacket.header.messageID[0]);
      Serial.println(coapPacket.header.messageID[1]);  */

    //Parsowanie tokena (jesli brak, przypisuje 0)
    if (coapPacket.header.tokenLength == 0) {
      coapPacket.token.value = NULL;
      coapPacket.token.lenght = 0;
    }
    else if (coapPacket.header.tokenLength <= 8) {
      coapPacket.token.value = packetBuffer + 4;
      coapPacket.token.lenght = coapPacket.header.tokenLength;

    }
    else {
      //niepoprawny rozmiar tokena
      //brak napisanego kodu z zalozenia ze copper zie nie myli
    }

    //Parsowanie opcji (jezeli sa)
    if (4 + coapPacket.header.tokenLength < packetSize) {
      uint8_t *byteWsk = packetBuffer + 4 + coapPacket.header.tokenLength;
      uint8_t *lastWsk = packetBuffer + packetSize;
      uint16_t delta = 0;
      int amountOfOptions = 0;
      while ((amountOfOptions < 16) && (byteWsk < lastWsk) && (*byteWsk != 0xFF)) {
        coapPacket.options[amountOfOptions];
        parseOption(&coapPacket, amountOfOptions, &delta, &byteWsk, lastWsk - byteWsk);
        amountOfOptions++;
      }
      coapPacket.numberOfOptions = amountOfOptions;
      if (byteWsk + 1 < lastWsk && *byteWsk == 0xFF) {
        coapPacket.payload.value = byteWsk + 1;
        coapPacket.payload.lenght = lastWsk - (byteWsk + 1);
      }
      else {
        coapPacket.payload.value = NULL;
        coapPacket.payload.lenght = 0;
      }
    }

    bool enter = false;  // zmienna do opcji accept (jesli pakiet jest nieakceptowalny, zmienia sie na true)
    bool etag = false;   // zmienna do etag (jesli znaleziono zmienia sie na true)


    int receiveEtag; // zapamietuje na ktorym numerze znajduje sie Etag (jezeli w ogole jest)
    /*for (int i = 0; i < coapPacket.numberOfOptions; i++) {

      Serial.println(coapPacket.options[i].optionNumber);
      Serial.println(coapPacket.options[i].lenght);
      Serial.println(*coapPacket.options[i].value);

      }*/

    //Szukanie opcji block2 w odebranym pakiecie
    for (int i = 0; i < coapPacket.numberOfOptions; i++) {
      if (coapPacket.options[i].optionNumber == 23 && coapPacket.options[i].lenght > 0) {
        //Jesli znaleziono, ustawia block2FOund na true i przypisuje jego wartosc do zmiennej value
        block2Found = true;
        block2Value = *coapPacket.options[i].value;
      }
      else
        block2Found = false;
    }

    //Szukanie opcji z etagiem
    for (int i = 0; i < coapPacket.numberOfOptions; i++) {
      if (coapPacket.options[i].optionNumber == 4 && coapPacket.options[i].lenght > 0) {
        //Jesli znaleziono, zmienia etag na true i przypisuje numer opcji do zmiennej
        //pod ktora kryje sie etag
        etag = true;
        receiveEtag = i;
      }
    }

    //Parsowanie uri-path do zmiennej temp (szukanie opcji nr 11)
    String url = "";
    for (int i = 0; i < coapPacket.numberOfOptions; i++) {
      if (coapPacket.options[i].optionNumber == 11 && coapPacket.options[i].lenght > 0) { //jesli znaleziono
        char urlname[coapPacket.options[i].lenght + 1];           //opcja 11 moze byc na wielu bajtach
        memcpy(urlname, coapPacket.options[i].value, coapPacket.options[i].lenght);
        urlname[coapPacket.options[i].lenght] = NULL; //zerowanie ostatniego znaku chara
        //jesil url jest wieksze od 0 ( czy juz cos dodano, a jest to juz kolejna opcja 11
        // dodaje '/' (jedna opcja to jedno 'wyrazenie')
        if (url.length() > 0)
          url += "/";
        url += urlname;      //dodanie do stringa odczytanej wartosci

      }
    }

    //kopia stringa do tablicy chara
    char temp[url.length()];
    url.toCharArray(temp, url.length() + 1);

    //Szuaknie opcji acceptable
    for (int i = 0; i < coapPacket.numberOfOptions; i++) {
      if (coapPacket.options[i].optionNumber == 17 && coapPacket.options[i].lenght >= 0) {
        //Jesli znaleziono opcje i jesli sciezka to wellknown core, akceptuje tylko link-format
        if (strcmp(temp, ".well-known/core") == 0) {
          if ((char)*coapPacket.options[i].value != 40) {
            enter = true;
          }
        }
        else {
          //W pozostalych przypadkach akceptuje tylko plain text
          if (coapPacket.options[i].lenght != 0) {
            enter = true;
          }
        }
      }
    }

    if (enter) {
      //Zmiana url na notAcceptable i przekopiowanie do temp, ktory jest charem
      url = "nA";
      url.toCharArray(temp, url.length() + 1);
    }

    //szukanie opcji observe
    for (int i = 0; i < coapPacket.numberOfOptions; i++) {
      if (coapPacket.options[i].optionNumber == 6 && coapPacket.options[i].lenght >= 0) {
        //jesli przyszla opcja observe a uri nie ejst stemplem, serwer ignoruje ta opcje
        if (strcmp(temp, "stempelCzasowy") == 0) {
          if (*coapPacket.options[i].value == 1) {
            subscribe = false;
            // Serial.println("koniec obserwacji");
          }
          else {
            // Serial.println("obserwacja");

            subscribe = true;
          }
        }
      }
    }
    //jesli przyszla prosba GET
    if (coapPacket.header.code == 1) {
      //Jesli przyszla opcja etag a dodatkowo uri jest lampka (tylko lampka obsluguje etag)
      if (etag) {
        if (strcmp(temp, "Lampka") == 0) {
          //jesli etag sie zgadza, odsyla valid, jesli nie - nowy pakiet
          if (*coapPacket.options[receiveEtag].value == (uint8_t*)eTagLampka)
            getFunc(&coapPacket, temp, false, etag, subscribe);
          else
            getFunc(&coapPacket, temp, true, etag, subscribe);
        }
        else
          getFunc(&coapPacket, temp, true, etag, subscribe);
      }
      //jesli przyszlo bez etagu, rowniez odsyla nowy pakiet
      else
        getFunc(&coapPacket, temp, true, etag, subscribe);
    }
    //Jesli przyszla opcja put i jesli uri to lampka, ustawia lampka na on off
    //jesli uri nie jest lampka, ignoruje
    //nie jest w tym przypadku odsylana zadna odpowiedz
    if (coapPacket.header.code == 3) {
      if (strcmp(temp, "Lampka") == 0) {
        String temp2 = (char*)coapPacket.payload.value;
        if (temp2[0] == 'O')
          if (temp2[1] == 'N')
          {
            putLight(true);
            eTagLampka = 't';
            //Serial.println("on");
          }
          else if (temp2[1] == 'F')
            if (temp2[2] == 'F')
            {
              eTagLampka = 'f';
              putLight(false);
            }
      }
    }
  }
}

//wysylanie pakietu do coppera
void sendPacket(coapPacket_t &coapPacket) {
  uint8_t buffer[MAX_BUFFER];
  uint8_t *wsk = buffer;
  uint16_t running_delta = 0;
  uint16_t packetSize = 0;
  //parsowanie naglowka
  *wsk = 0x01 << 6;
  *wsk |= (coapPacket.header.messageType & 0x03) << 4;
  *wsk++ |= (coapPacket.header.tokenLength & 0x0F);
  *wsk++ = coapPacket.header.code;
  *wsk++ = coapPacket.header.messageID[0];
  *wsk++ = coapPacket.header.messageID[1];
  wsk = buffer + 4;
  packetSize += 4;

  // parsowanie tokena (jesli jest potrzeba)
  if (coapPacket.token.value != NULL && coapPacket.token.lenght <= 0x0F) {
    memcpy(wsk, coapPacket.token.value, coapPacket.token.lenght);
    wsk += coapPacket.token.lenght;
    packetSize += coapPacket.token.lenght;
  }

  // parsowanie opcji
  for (int i = 0; i < coapPacket.numberOfOptions; i++) {
    uint32_t optdelta;
    uint8_t len, delta;

    if (packetSize + 5 + coapPacket.options[i].lenght < MAX_BUFFER) {

      optdelta = coapPacket.options[i].optionNumber - running_delta;
      COAP_OPTION_DELTA(optdelta, &delta);
      COAP_OPTION_DELTA((uint32_t)coapPacket.options[i].lenght, &len);

      *wsk++ = (0xFF & (delta << 4 | len));
      if (delta == 13) {
        *wsk++ = (optdelta - 13);
        packetSize++;
      }
      else if (delta == 14) {
        *wsk++ = ((optdelta - 269) >> 8);
        *wsk++ = (0xFF & (optdelta - 269));
        packetSize += 2;
      } if (len == 13) {
        *wsk++ = (coapPacket.options[i].lenght - 13);
        packetSize++;
      }
      else if (len == 14) {
        *wsk++ = (coapPacket.options[i].lenght >> 8);
        *wsk++ = (0xFF & (coapPacket.options[i].lenght - 269));
        packetSize += 2;
      }

      memcpy(wsk, coapPacket.options[i].value, coapPacket.options[i].lenght);
      wsk += coapPacket.options[i].lenght;
      packetSize += coapPacket.options[i].lenght + 1;
      running_delta = coapPacket.options[i].optionNumber;
    }
  }

  // parsowanie payload
  if (coapPacket.payload.lenght > 0) {
    if ((packetSize + 1 + coapPacket.payload.lenght) < MAX_BUFFER) {
      *wsk++ = 0xFF;
      memcpy(wsk, coapPacket.payload.value, coapPacket.payload.lenght);
      packetSize += 1 + coapPacket.payload.lenght;
    }
  }
  // wyslanie sparsowanego pakietu
  udp.beginPacket(udp.remoteIP(), udp.remotePort());
  udp.write(buffer, packetSize);
  udp.endPacket();
}

//obliczanie rozmiaru pakietu na potrzeby size2
//jest to ta sama funkcja co sendPacket wyzej
//z usunietymi memcpy i wysylaniem pakietu
//czyli interacji w pakiet
uint8_t packetSize(coapPacket_t &coapPacket) {
  uint8_t buffer[MAX_BUFFER];
  uint8_t *wsk = buffer;
  uint16_t running_delta = 0;
  uint8_t packetSize = 0;
  *wsk = 0x01 << 6;
  *wsk |= (coapPacket.header.messageType & 0x03) << 4;
  *wsk++ |= (coapPacket.header.tokenLength & 0x0F);
  *wsk++ = coapPacket.header.code;
  *wsk++ = coapPacket.header.messageID[0];
  *wsk++ = coapPacket.header.messageID[1];
  wsk = buffer + 4;
  packetSize += 4;
  if (coapPacket.token.value != NULL && coapPacket.token.lenght <= 0x0F) {
    wsk += coapPacket.token.lenght;
    packetSize += coapPacket.token.lenght;
  }
  for (int i = 0; i < coapPacket.numberOfOptions; i++) {
    uint32_t optdelta;
    uint8_t len, delta;
    if (packetSize + 5 + coapPacket.options[i].lenght < MAX_BUFFER) {
      optdelta = coapPacket.options[i].optionNumber - running_delta;
      COAP_OPTION_DELTA(optdelta, &delta);
      COAP_OPTION_DELTA((uint32_t)coapPacket.options[i].lenght, &len);
      *wsk++ = (0xFF & (delta << 4 | len));
      if (delta == 13) {
        *wsk++ = (optdelta - 13);
        packetSize++;
      }
      else if (delta == 14) {
        *wsk++ = ((optdelta - 269) >> 8);
        *wsk++ = (0xFF & (optdelta - 269));
        packetSize += 2;
      } if (len == 13) {
        *wsk++ = (coapPacket.options[i].lenght - 13);
        packetSize++;
      }
      else if (len == 14) {
        *wsk++ = (coapPacket.options[i].lenght >> 8);
        *wsk++ = (0xFF & (coapPacket.options[i].lenght - 269));
        packetSize += 2;
      }
      wsk += coapPacket.options[i].lenght;
      packetSize += coapPacket.options[i].lenght + 1;
      running_delta = coapPacket.options[i].optionNumber;
    }
  }
  if (coapPacket.payload.lenght > 0) {
    if ((packetSize + 1 + coapPacket.payload.lenght) < MAX_BUFFER) {
      *wsk++ = 0xFF;
      packetSize += 1 + coapPacket.payload.lenght;
    }
  }
  return packetSize;
}


//obsluga pakietu i zadan, tworzenie odpowiedzi
void getFunc(coapPacket_t *coapPacket, char *url, bool newPacket, bool etag, bool observe) {
  //sprawdza, czy serwer ma polaczeniz arduino mini przez
  //od razu zalatwiana jest kwestia stanu lampki
  bool git = getLightStatus();

  wyslane += 1;
  //utworzenie naglowka
  reply.header.ver = coapPacket->header.ver;
  reply.header.messageType = 1;

  //jesli accept sie nie zgadza
  //tworzy wiadomosc nonAcceptable
  if (strcmp(url, "nA") == 0) {
    reply.header.code = (4 << 5) | (6);
  }

  //jesli nowy pakiet tworzy ack, jesli nie - valid
  //etag
  else if (newPacket)
    reply.header.code = (2 << 5) | (5);
  else
    reply.header.code = (2 << 5) | (3);

  //jesli serwer nie mogl sie polaczyc z arduino mini
  //tworzy naglowek service unavailible
  if (!git)
    reply.header.code = (5 << 5) | (3);

  //pozostala czesc naglowka dla odpowiedzi
  //kopia tokena i MID

  reply.header.tokenLength = coapPacket->header.tokenLength;
  reply.header.messageID[0] = (rand() % 155) + 100;
  reply.header.messageID[1] = (rand() % 155) + 100;
  reply.token.value = coapPacket->token.value;
  reply.token.lenght = coapPacket->token.lenght;
  reply.numberOfOptions = 0;

  //na potrzeby opcji content-format
  char optionBuffer[2];

  //na potrzeby size2
  uint8_t sizeBuffer;
  int numberOfSize2;

  //jesli arduino mini jest dostepne
  if (git) {
    //jesli jest porzeba przyslania swiezego pakietu
    if (newPacket) {

      //jesli url to lampka
      //lampka obsluguje etag, konieczne jest przypisanie opcji
      if (strcmp(url, "Lampka") == 0) {
        if (eTagLampka == 'f')
          reply.options[reply.numberOfOptions].value = ("f");
        else
          reply.options[reply.numberOfOptions].value = ("t");

        reply.options[reply.numberOfOptions].lenght = 1;
        reply.options[reply.numberOfOptions].optionNumber = 4;
        reply.numberOfOptions++;
      }

      //jesli accept sie zgadza, wchodzi
      //jesli nie, nie ustawia content-format
      //odsyla wtedy pusty pakiet
      if (strcmp(url, "nA") != 0) {
        if (strcmp(url, ".well-known/core") == 0) {
          optionBuffer[0] = ((uint16_t)40 & 0xFF00) >> 8;
          optionBuffer[1] = ((uint16_t)40 & 0x00FF);
        }
        else {
          optionBuffer[0] = ((uint16_t)0 & 0xFF00) >> 8;
          optionBuffer[1] = ((uint16_t)0 & 0x00FF);
        }
        reply.options[reply.numberOfOptions].value = (uint8_t *)optionBuffer;
        reply.options[reply.numberOfOptions].lenght = 2;
        reply.options[reply.numberOfOptions].optionNumber = 12;
        reply.numberOfOptions++;
      }


      //obsluga block2
      //block2 u nas nie wyjdzie poza jeden bit
      //val to sparsowane trzy wartosci NUM/M/SZX
      uint8_t val;

      //wartosc sciezki (dziala prawidlowo tylko w taki smieszny sposob)
      //(inaczej przysyla jakies niechciane, losowe wartosci)
      char* core = "";



      //block2 tyczy sie tylko zasobu duzego
      //nasz zasob duzy to wellknown/core
      if (strcmp(url, ".well-known/core") == 0) {
        int num;  //numer bloku
        int more; //czy wiecej
        int siz;  //rozmiar bloku

        //jesli cooper nie przyslal block2, ustawia wartosci domyslne
        //numer pierwszy, czy wiecej - nie, rozmiar bloku - 16
        //num i more sa wartosciami tymczasowymi, ulegana potem zmianie
        if (!block2Found) {
          num = 1;
          more = 0;
          siz = 0;
        }

        //jesli znaleziono block2, parsowanie wartosci z pakietu do zmiennych
        else {
          siz = (block2Value & 0x07);
          more = (block2Value >> 3) & 0x01;
          num = (block2Value >> 4) & 0x0F;
        }

        //obsluga rozmiaru 0 -> 16b
        if (siz == 0) {
          if (num == 1) {
            core = "</.well-known/co";
            more = 1;
          }
          else if (num == 2) {
            core = "re>, </.well-kno";
            more = 1;
          }
          else if (num == 3) {
            core = "wn/metryki>, </s";
            more = 1;
          }
          else if (num == 4) {
            core = "tempelCzasowy>, ";
            more = 1;
          }
          else if (num == 5) {
            core = "</Lampka>";
            more = 0;
          }
        }

        //obsluga rozmiaru 1 -> 32b
        else if (siz == 1) {
          if (num == 1) {
            core = "</.well-known/core>, </.well-kno";
            more = 1;
          }
          if (num == 2) {
            core = "wn/metryki>, </stempelCzasowy>, ";
            more = 1;
          }
          else if (num == 3) {
            core = "</Lampka>";
            more = 0;
          }
        }
        else{
          core = NULL;
          more = 0;
        }
        //dla innych wartosci block2 jest brak obslugi
        //ze wzgledu na koniecznosc oszczedzania pamieci w arduino pro


        //parsowanie ustalonych wartosci block2 do val i utworzenie opcji
        //w odpowiedzi
        val = 0;
        num = num << 4;
        val = val | num;
        more = more << 3;
        val = val | more;
        val = val | siz;

        reply.options[reply.numberOfOptions].value = &val;
        reply.options[reply.numberOfOptions].lenght = 1;
        reply.options[reply.numberOfOptions].optionNumber = 23;
        reply.numberOfOptions++;
      }


      //obsluga size2
      //rowniez tyczy sie to tylko zasobu duzego
      if (strcmp(url, ".well-known/core") == 0) {
        sizeBuffer = 60;   //jest to tylko poczatkowa, orientacyjna wartosc, pozniej jest liczona rzeczywista
        reply.options[reply.numberOfOptions].value = &sizeBuffer;
        reply.options[reply.numberOfOptions].lenght = 1;
        reply.options[reply.numberOfOptions].optionNumber = 28;
        numberOfSize2 = reply.numberOfOptions;
        reply.numberOfOptions++;
        reply.payload.value = (uint8_t*)(core);
      }
      // zeby pozniej zliczyc rzeczywisty rozmiar pakietu, musimy miec opcje size2
      // pozniej bedzie zliczany rozmiar z funkcji ktora znajduje sie wyzej
      // rozmiar zmiesci sie na jednym bajcie

      // odeslanie payloadu dla lampki
      else if (strcmp(url, "Lampka") == 0) {
        if (stat.stan == true) {
          reply.payload.value = (uint8_t*)("Lampka jest wlaczona");
        }
        else if (stat.stan == false) {
          reply.payload.value = (uint8_t*)("Lampka jest wylaczona");
        }
      }

      // obsluga subskrypcji stempla czasowego
      // jesli uri to stempel, tylko wtedy rozwaza subskrypcje
      // w innych przypadkach observe jest ignorowane
      else if (strcmp(url, "stempelCzasowy") == 0) {
        if (observe) {
          //tworzenie payloadu dla subskrypcji
          char temp[8];
          int temp1 = 0;
          int temp2 = payloadTime.hour;
          for (int i = 0; i < 10; i++) {
            if (temp2 - 10 < 0)
              break;
            else {
              temp2 = temp2 - 10;
              temp1++;
            }
          }
          temp[0] = temp1 + 48;
          temp[1] = temp2 + 48;
          temp[2] = ':';

          temp1 = 0;
          temp2 = payloadTime.mins;
          for (int i = 0; i < 10; i++) {
            if (temp2 - 10 < 0)
              break;
            else {
              temp2 = temp2 - 10;
              temp1++;
            }
          }
          temp[3] = temp1 + 48;
          temp[4] = temp2 + 48;
          temp[5] = ':';

          temp1 = 0;
          temp2 = payloadTime.secs;

          for (int i = 0; i < 10; i++) {
            if (temp2 - 10 < 0)
              break;
            else {
              temp2 = temp2 - 10;
              temp1++;
            }
          }
          temp[6] = temp1 + 48;
          temp[7] = temp2 + 48;
          char* czas = "        ";
          for (int i = 0; i < 8; i++)
            czas[i] = temp[i];
          reply.payload.value = (uint8_t*)(czas);
        }
        //jesli stempel nie jest subskrybowany
        //wyswietla odpowiedni payload
        else
          reply.payload.value = (uint8_t*)("Brak suba");
        reply.payload.lenght = strlen(reply.payload.value);
      }

      //Obsluga zestawu metryk
      //=========================================================================================================================================================================================================
      else if (strcmp(url, ".well-known/metryki") == 0) {
        coapPacket_t ping;
        ping.header.ver = coapPacket->header.ver;
        ping.header.messageType = 0;
        ping.header.code = (0 << 5) | (0);
        ping.header.tokenLength = 0;
        ping.header.messageID[0] = coapPacket->header.messageID[0] >> 1;
        ping.header.messageID[1] = coapPacket->header.messageID[1] << 1;
        ping.token.value = NULL;
        ping.token.lenght = 0;
        ping.numberOfOptions = 0;
        ping.payload.value = NULL;
        ping.payload.lenght = 0;
        wyslane += 1;
        int start = millis();
        sendPacket(ping);
        for (;;) {
          if (udp.parsePacket()>0) {
            del = millis() - start;
            odebrane += 1;
            break;
          }
        }
        for (int i = 0; i < 50; i++)
          metryka[i] = NULL;


        sprintf(metryka, "Odebrane: %d, Wyslane: %d, Ping: %d", odebrane, wyslane, del);

        reply.payload.value = (uint8_t *)&metryka;
      }

      //jesli pakiet nie jest akceptowalny (zly accept)
      //usuwa payload, odsylany jest pusty pakiet
      else if (strcmp(url, "nA") == 0) {
        reply.payload.value = NULL;
        reply.payload.value = 0;
      }
      // obsluga wszelkich pozostalych przypadkow z url
      else
        reply.payload.value = (uint8_t*)("Brak obslugi");
      reply.payload.lenght = strlen(reply.payload.value);

      //obliczanie rzeczywistego rozmiaru pakietu i przypisanie go do wartosci opcji
      if (strcmp(url, ".well-known/core") == 0) {
        sizeBuffer = packetSize(reply);
        reply.options[numberOfSize2].value = &sizeBuffer;
      }
    }

    //jesli nie ma potrzeby odsylania swiezego pakietu
    //czyli obsluga poprawnego etag dla lampki
    else {
      if (etag) {
        if (eTagLampka == 'f')
          reply.options[reply.numberOfOptions].value = ("f");
        else
          reply.options[reply.numberOfOptions].value = ("t");
        reply.options[reply.numberOfOptions].lenght = 1;
        reply.options[reply.numberOfOptions].optionNumber = 4;
        reply.numberOfOptions++;

        reply.payload.value = NULL;
        reply.payload.lenght = 0;
      }
      //jesli nie znaleziono etag w przychodzacym pakiecie
      //wyswietli blad
      //teoretycznie nigdy go nie ma, poniewaz wczesniej etag jest sprawdzany, tzn
      //jesli jest, to porownuje go z obecnym stanem lampki i odsyla nowy, albo valid
      //jesli nie to zawsze odsyla nowy
      else {
        reply.payload.value = (uint8_t*)("Brak obslugi");
        reply.payload.lenght = strlen(reply.payload.value);
      }
    }
  }

  //jesli arduino mini niedostepne
  //(ostatecznie wysyla pusty pakiet z service unavailible)
  else {
    reply.payload.value = NULL;
    reply.payload.lenght = 0;
  }
  sendPacket(reply);

}

//parsowanie opcji z pakietu przychodzacego
int parseOption(coapPacket_t *coapPacket, int amountOfOptions, uint16_t *running_delta, uint8_t **buf, size_t buflen) {
  uint8_t *p = *buf;
  uint8_t headlen = 1;
  uint16_t len, delta;

  if (buflen < headlen) {
    Serial.println("1");
    return 0;
  }

  delta = (p[0] & 0xF0) >> 4;
  len = p[0] & 0x0F;

  if (delta == 13) {
    headlen++;
    if (buflen < headlen) {
      Serial.println("2");
      return 0;
    }
    delta = p[1] + 13;
    p++;
  }
  else if (delta == 14) {
    headlen += 2;
    if (buflen < headlen) {
      Serial.println("3");
      return 0;
    }
    delta = ((p[1] << 8) | p[2]) + 269;
    p += 2;
  }
  else if (delta == 15) {
    Serial.println("4");
    return 0;
  }

  if (len == 13) {
    headlen++;
    if (buflen < headlen) {
      Serial.println("5");
      return 0;
    }
    len = p[1] + 13;
    p++;
  }
  else if (len == 14) {
    headlen += 2;
    if (buflen < headlen) {
      Serial.println("6");
      return 0;
    }
    len = ((p[1] << 8) | p[2]) + 269;
    p += 2;
  }
  else if (len == 15)
  {
    Serial.println("7");
    return 0;
  }

  if ((p + 1 + len) > (*buf + buflen)) {
    Serial.println("8");
    return 0;
  }
  coapPacket->options[amountOfOptions].optionNumber = delta + *running_delta;
  coapPacket->options[amountOfOptions].value = p + 1;
  coapPacket->options[amountOfOptions].lenght = len;
  *buf = p + 1 + len;
  *running_delta += delta;

  return 0;
}


//ustalenie serial, polaczenia radiowego z arduino mini
void setupHardware() {
  //===========================================================
  //SERIAL I RADIO
  Serial.begin(115200);
  SPI.begin();
  radio.begin();
  network.begin(105, 00);
}

//wlaczenie lub wylaczenie lampki w arduino mini
//wyslanie droga radiowa odpowiedniej prosby
void putLight(bool status) {
  RF24NetworkHeader header1(01);
  if (status == true) {
    payloadm = { true, false, false };
  }
  else if (status == false) {
    payloadm = { false, true, false };
  }
  network.update();
  if (network.write(header1, &payloadm, sizeof(payloadm)));
  else
    Serial.println("Nie wyslano");
}

//jesli zostal nacisniety stamp, zwraca true
//jesli nie - false
//pobieranie czasu z arduino mini
bool getTimeStamp() {
  network.update();

  while (network.available()) {

    RF24NetworkHeader header;
    network.read(header, &payloadTime, sizeof(payloadTime));
    /* if (payloadTime.hour < 10) {
       Serial.print("0");
       Serial.print(payloadTime.hour);
      }
      else
       Serial.print(payloadTime.hour);
      Serial.print(":");
      if (payloadTime.mins < 10) {
       Serial.print("0");
       Serial.print(payloadTime.mins);
      }
      else
       Serial.print(payloadTime.mins);
      Serial.print(":");
      if (payloadTime.secs < 10) {
       Serial.print("0");
       Serial.print(payloadTime.secs);
      }
      else
       Serial.print(payloadTime.secs);
      Serial.println();*/
    return true;
  }

  return false;
}

//pobranie stanu lampki z arduino mini
//jesli jest polaczenie z arduino, zwroci true
//jesli nie - false
//jest to tez metoda sprawdzenia czy jest polaczenie z arduino mini
//czy service unavailible
bool getLightStatus() {
  network.update();
  RF24NetworkHeader header1(01);
  message payload = { false, false, true };
  if (network.write(header1, &payload, sizeof(payload)));
  else {
    Serial.println("Nie wyslano");
    return false;
  }
  delay(10);
  network.update();
  while (network.available()) {

    RF24NetworkHeader header;
    network.read(header, &stat, sizeof(stat));

  }
  return true;
}
