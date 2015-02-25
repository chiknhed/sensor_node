#include <SPI.h>
#include <WiFi.h>
#include <sha1.h>
#include <DHT.h>
#include <avr/wdt.h>

#include <EEPROM.h>

//#define ECHO_ON
//#define PRINT_POST_RESULT

/* sensor definitions and variables */
#define DHTPIN 2
#define DHTTYPE DHT22
DHT dht(DHTPIN, DHTTYPE);

/* wifi definitions and variables */
#define IDLE_TIMEOUT_MS  5000
WiFiClient www;

/* wifi connection definitions and variables */
#define MAX_SSID 33
#define MAX_PASSWD 20

/* web connection definitions and variables */
#define IDLE_MEASURE_COUNT 10
#define SERVER_NAME "galvanic-cirrus-841.appspot.com"
#define SERVER_PORT 80
#define INPUT_PAGE "/sensor/input/"

char msgHeader[100];
char postData[40];
int firstReport = 1;

int freeRam () {
  extern int __heap_start, *__brkval;
  int v;
  return (int) &v - (__brkval == 0 ? (int) &__heap_start : (int) __brkval);
}
void readEeprom(char * ssid, char * passwd, char *security) {
  int i;

  Serial.println(F("reading EEPROM.."));
  for (i = 0; i < MAX_SSID - 1; i++) {
    ssid[i] = EEPROM.read(i);
  }
  ssid[MAX_SSID-1] = 0;
  for (i = 0; i < MAX_PASSWD - 1; i++) {
    passwd[i] = EEPROM.read(i + MAX_SSID);
  }
  passwd[MAX_PASSWD-1] = 0;
  *security = EEPROM.read(MAX_SSID + MAX_PASSWD);
}

void writeEeprom(char * ssid, char * passwd, char security) {
  int i;

  Serial.println("Writing EEPROM");

  for (i = 0; i < MAX_SSID; i++) {
    EEPROM.write(i, ssid[i]);
  }
  for (i = 0; i < MAX_PASSWD; i++) {
    EEPROM.write(i + MAX_SSID, passwd[i]);
  }
  EEPROM.write(MAX_SSID + MAX_PASSWD, security);
}

/* ugly ugly */
/* get the line input : the buffer should be big enough to handle \r \n \0 */
void getLineInput(char * buffer, int len) {
  int i = 0;

  for (i = 0; i<len - 1; i++) {
    byte bytes;
    do {
      bytes = Serial.readBytes(&buffer[i], 1);
    } 
    while (bytes == 0);
    if (buffer[i] == '\r' || buffer[i] == '\n') {
      char trashTrail;
#ifdef ECHO_ON
      Serial.println();
#endif
      buffer[i] = 0;
      Serial.setTimeout(100);
      Serial.readBytes(&trashTrail, 1);  //remove trailing \r or \n
      return;
    }
#ifdef ECHO_ON
    Serial.print(buffer[i]);
#endif
  }
  buffer[i] = 0;
#ifdef ECHO_ON
  Serial.println();
#endif
}

void getInput() {
  int i;
  char sec[3];
  
  char ssid[MAX_SSID];
  char passwd[MAX_PASSWD];
  char security;


  Serial.print(F("SSID : "));
  getLineInput(ssid, MAX_SSID);

  do {
    Serial.print(F("Security : "));
    getLineInput(sec, 3);
    security = sec[0] - '0';
  } 
  while (security > 4 || security < 0);
  security = encSaveType(security);

  if (security != 0) {
    Serial.print(F("Password : "));
    getLineInput(passwd, MAX_PASSWD);
  }
  
  writeEeprom(ssid, passwd, security);
}

int buildSecureKey(char* macString, char* secureKey) {
  uint8_t* hash;
  int i;

  Sha1.init();
  Sha1.print("owen77");
  Sha1.print(macString);
  Sha1.print("young");
  
  hash = Sha1.result();

  for (i = 0; i < 20; i++) {
    secureKey[i*2] = "0123456789abcdef"[hash[i]>>4];
    secureKey[i*2+1] = "0123456789abcdef"[hash[i]&0xf];
  }
  secureKey[i*2] = 0;

  return 0;
}

int buildMacString(byte* mac, char* macString, byte sec) {
  int i;
  int stringIndex = 0;

  for (i = 0; i < 6; i++) {
    macString[stringIndex++] = "0123456789abcdef"[mac[i]>>4];
    macString[stringIndex++] = "0123456789abcdef"[mac[i]&0xf];
    if (i != 5) {
      if (!sec) {
        macString[stringIndex++] = '-';
      } 
      else {
        macString[stringIndex++] = '%';
        macString[stringIndex++] = '3';
        macString[stringIndex++] = 'A';
      }
    }
  }
  macString[stringIndex] = 0;

  return 0;
}

int buildMsgHeader() {
  byte mac[6];
  char macString[18];
  char macEncString[28];
  char secureKey[41];

  WiFi.macAddress(mac);
  buildMacString(mac, macString, 0);
  Serial.print(F("mac: "));
  Serial.println(macString);
  buildMacString(mac, macEncString, 1);
  buildSecureKey(macString, secureKey);
  
  sprintf(msgHeader, "secure_key=%s&mac_address=%s", secureKey, macEncString);
  
  return 0;
}

int connectAp(byte trials) {
  char ssid[MAX_SSID];
  char passwd[MAX_PASSWD];
  char security;
  byte i;
  int status = WL_IDLE_STATUS;
  
  wdt_disable();
  readEeprom(ssid, passwd, &security);
  
  Serial.println(F("connecting to AP.. "));
  if (WiFi.status() == WL_NO_SHIELD) {
    Serial.println("WiFi shield not present");
    // don't continue:
    while (true);
  }
  
  String fv = WiFi.firmwareVersion();
  if ( fv != "1.1.0" )
    Serial.println("Please upgrade the firmware");
  
  for (i = 0; i < trials; i++) {
    if (security)
      status = WiFi.begin(ssid, passwd);
    else
      status = WiFi.begin(ssid);
    delay(5000);
    if (WiFi.status() == WL_CONNECTED) {
      break;
    }
  }

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println(F("AP connected 0"));
    wdt_enable(WDTO_8S);
    return -1;
  }

  Serial.println(F("AP connected 1"));
  wdt_enable(WDTO_8S);

  return 0;
}

int encSaveType(int intType) {
  switch (intType) {
  case 1:
    return ENC_TYPE_WEP;
  case 2:
    return ENC_TYPE_TKIP;
  case 3:
    return ENC_TYPE_CCMP;
  case 0:
    return ENC_TYPE_NONE;
  default:
    return ENC_TYPE_AUTO;
  }
}

int encType(int thisType) {
  switch (thisType) {
  case ENC_TYPE_WEP:
    return 1;
  case ENC_TYPE_TKIP:
    return 2;
  case ENC_TYPE_CCMP:
    return 3;
  case ENC_TYPE_NONE:
    return 0;
  case ENC_TYPE_AUTO:
    return 3;
  }
}

void scanNetworks() {
  char b;

  if (WiFi.status() == WL_NO_SHIELD) {
    Serial.println("WiFi shield not present");
    // don't continue:
    while (true);
  }

  int numSsid = WiFi.scanNetworks();
  Serial.println("Scan result : ");

  for (int thisNet = 0; thisNet < numSsid; thisNet++) {
    Serial.println(WiFi.SSID(thisNet));
    Serial.println(WiFi.RSSI(thisNet));
    Serial.println(encType(WiFi.encryptionType(thisNet)));
  }
  Serial.setTimeout(0xffffff);
  Serial.readBytes(&b, 1);
}

void setup() {
  int i;
  int firstTrial = 1;
  char tempInput[1];
  byte connected = 0;

  wdt_disable();
  
  Serial.begin(9600);
  
  /* check if user want update the input */
  if (Serial) {
    Serial.println(F("Press 'c' key to update AP settings : "));
    Serial.setTimeout(5000);
    if (Serial.readBytes(tempInput, 1)) {
      if (tempInput[0] == 'c') {
        do {
          Serial.println(F("Press 'a' to set AP settings, Press 's' to scan APs : "));
          Serial.setTimeout(0xffffff);
          if (Serial.readBytes(tempInput, 1)) {
            if (tempInput[0] == 'a') {
              getInput();
              if (!connectAp(2)) {
                connected = 1;
                break;
              }
              buildMsgHeader();
            } 
            else if (tempInput[0] == 's') {
              scanNetworks();
            } 
            else if (tempInput[0] == 'b') {
              break;
            }
          }
        }
        while (! connected);
      } 

    }
  }

  if (!connected) {
    while (connectAp(2)) {
        /* read AP connections settings */  
      wdt_enable(WDTO_8S);
      firstTrial = 0;
      while (!Serial) {
        ;
      }
      getInput();
    }
  }

  /* read AP connections settings */  
  wdt_enable(WDTO_8S);
  
  /* initialize thermal/humidity sensor */
  dht.begin();

  /* build post message prefix from MAC string */
  buildMsgHeader();

  /* set first report flag */
  firstReport = 1;
}

#define ERROR_VAL 9874

enum parseStatus {
  NONE_STATUS,
  PLUS_STATUS,
  VALUE_STATUS,
  EQUAL_STATUS,
  EXIT_STATUS,
};

byte postPage(char* thisData, char* thatData, int val[3])
{
  byte ret, isSigned, valIndex, i;
  int * pVal;
  int status;
  byte rx_byte = 0;
  enum parseStatus parState = NONE_STATUS;

  wdt_reset();
  status = www.connect(SERVER_NAME, SERVER_PORT);
  wdt_reset();
  
  if(www.connected())
  {
    char length_buffer[10];
    Serial.println(F("connected"));
    wdt_reset();

    www.println(F("POST " INPUT_PAGE " HTTP/1.1"));
    www.print(F("Host: "));
    www.println(F(SERVER_NAME));
    www.println(F("Connection: close"));
    www.println(F("Content-Type: application/x-www-form-urlencoded"));
    www.print(F("Content-Length: "));
    
    sprintf(length_buffer, "%u", strlen(thisData) + strlen(thatData));
    www.println(length_buffer);
    www.println();

    int dataLen = strlen(thisData);
    char * pos = thisData;
    do {
      int written = www.write(pos, 10);
      dataLen -= written;
      pos += written;
      delay(10);
    }
    while (dataLen > 0);
    
    dataLen = strlen(thatData);
    pos = thatData;
    do {
      int written = www.write(pos, 10);
      dataLen -= written;
      pos += written;
      delay(10);
    }
    while (dataLen > 0);
    
    www.println();
  }
  else
  {
    Serial.print(F("failed"));
    Serial.println(status);
    return 0;
  }

  Serial.print(F("posted : "));
  Serial.print(strlen(thisData));
  Serial.println(F(" bytes"));
  wdt_reset();

  int connectLoop = 0;

  unsigned long lastRead = millis();
  valIndex = 0;
  while (www.connected() && (millis() - lastRead < IDLE_TIMEOUT_MS)) {
    wdt_reset();
    while (www.available()) {
      char c = www.read();
#ifdef PRINT_POST_RESULT
      Serial.print(c);
#endif
      rx_byte ++;
      lastRead = millis();

      if (parState != EXIT_STATUS && c == '+') {
        isSigned = 0;
        parState = VALUE_STATUS;
        pVal = &val[valIndex++];
        *pVal = 0;
      } 
      else if (parState == VALUE_STATUS) {
        if (c == '-') {
          isSigned = 1;
        } 
        else if (c == 'z') {
          //reset the device
          while(true);
        } 
        else if (c == 'e') {
          *pVal = ERROR_VAL;
          parState = EXIT_STATUS;
        }
        else if (c == 'n') {
          *pVal = ERROR_VAL;
          parState = EQUAL_STATUS;
        } 
        else if (c == '=') {
          if (isSigned) *pVal = - *pVal;
          parState = EQUAL_STATUS;
        } 
        else if (c == '.') {
          parState = EQUAL_STATUS;
        }
        else {
          *pVal *= 10;
          *pVal += c - '0';
        }
      } 
    }
  }
  www.stop();
  Serial.print(F("read : "));
  Serial.print(rx_byte);
  Serial.println(F(" bytes"));
  wdt_reset();

  return 1;
}

/* report data through POST and get the setting values */
int report_data(int sensor_type, float value, unsigned long * report_period, int* high_threshold, int* low_threshold) {
  int wifiStatus;
  int ret;
  int val[3] = {
    ERROR_VAL, ERROR_VAL, ERROR_VAL    };
  int rssi = -60;
  
  wdt_reset();
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println(F("disconnected"));
    connectAp(1);
  }
  wdt_reset();
  sprintf(postData, "&type=%d&value=%d&rssi=%d&first=%d", sensor_type, (int)(value*10.0), rssi, firstReport);
  ret = !postPage(msgHeader, postData, val);
  wdt_reset();

  if (ret) {
    return -1;
  }

  firstReport = 0;

  if (val[0] == ERROR_VAL) {
    Serial.println(F("Getting settings failed"));
    return -1;
  }
  *report_period = val[0];
  *high_threshold = val[1];
  *low_threshold = val[2];
  if (*high_threshold == ERROR_VAL) *high_threshold = 1000;
  if (*low_threshold == ERROR_VAL) *low_threshold = -1000;

  Serial.println(F("cfg"));
  Serial.println(sensor_type);
  Serial.println(*report_period);
  Serial.println(*high_threshold);
  Serial.println(*low_threshold);

  return ret;
}

int outRange(float value, int low, int high)
{
  int intVal = (int)(value + 0.5);

  if (intVal < low) return 1;
  if (intVal > high) return 1;
  return 0;
}

void loop() {
  int outRangeReported = 0;
  int lowTh1, highTh1, lowTh0, highTh0;
  byte i, loop_count = IDLE_MEASURE_COUNT;
  unsigned long reportPeriod = 600;
  unsigned long measurePeriod;
  float temperature, humidity;
  int value;
  unsigned long first, last, target;

  /* start Watch dog. */
  wdt_enable(WDTO_8S);

  while(true) {
    first = millis();
    Serial.println();
    Serial.print(F("TS measure:")); 
    Serial.println(first);

    wdt_reset();
    dht.read();
    wdt_reset();
    temperature = dht.readTemperature();
    wdt_reset();
    humidity = dht.readHumidity();

    wdt_reset();
    loop_count = IDLE_MEASURE_COUNT;
    Serial.println();
    Serial.print(F("TS temper report:"));
    Serial.println(millis());
    if (temperature != NAN && !report_data(0, temperature, &reportPeriod, &highTh0, &lowTh0)) {
      Serial.println(F("reported"));
    } 
    else {
      Serial.println(F("adjust loop count"));
      loop_count /= 2;
    }
    wdt_reset();
    Serial.print(F("TS delay 4000 : "));
    Serial.println(millis()); 
    delay(4000);
    wdt_reset();
    Serial.print(F("TS humid report:"));
    Serial.println(millis()); 
    if (humidity != NAN && !report_data(1, humidity, &reportPeriod, &highTh1, &lowTh1)) {
      Serial.println(F("reported"));
    } 
    else {
      Serial.println(F("adjust loop count"));
      loop_count /= 2;
    }
    Serial.print(F("TS report finish:")); 
    Serial.println(millis()); 
    wdt_reset();

    if (reportPeriod < 120UL)
      reportPeriod = 120;
    measurePeriod = reportPeriod / (unsigned long) IDLE_MEASURE_COUNT;
    
    if (measurePeriod == 0) measurePeriod = 1;
    
    first = millis() - first;
      
    for (i = 0; i < loop_count; i++) {
      last = millis();
      Serial.println();
      Serial.print(F("TS:")); 
      Serial.println(last);
      Serial.println(F("value"));

      wdt_reset();
      dht.read();
      wdt_reset();
      temperature = dht.readTemperature();
      wdt_reset();
      humidity = dht.readHumidity();

      Serial.println(temperature);
      Serial.println(humidity);
      wdt_reset();

      if (!outRangeReported && outRange(temperature, lowTh0, highTh0) || outRange(humidity, lowTh1, highTh1)) {
        outRangeReported = 1;
        break;
      }

      if (outRangeReported && !outRange(temperature, lowTh0, highTh0) && !outRange(humidity, lowTh1, highTh1)) {
        outRangeReported = 0;
        break;
      }
      last = millis() - last;
      if (last + first < (measurePeriod * 1000UL)){
        target = (measurePeriod*1000UL) - last - first;
        do {
          wdt_reset();
          target -= 4000UL;
          delay(4000UL);
          //Serial.println(target);
        }
        while (target >= 4000UL); 
        wdt_reset();
        delay(target);        
      }
      wdt_reset();
      first = 0;
    }
  }  
}
