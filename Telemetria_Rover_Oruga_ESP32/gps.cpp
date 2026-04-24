#include "gps.h"

HardwareSerial GPS(2);

float gps_lat = 0;
float gps_lon = 0;
float gps_speed = 0;
float gps_course = 0;
int gps_sats = 0;
bool gps_fix = false;

String line = "";

float convLat(String v, String h){
  float x = v.substring(0,2).toFloat() + v.substring(2).toFloat()/60.0;
  if(h=="S") x=-x;
  return x;
}

float convLon(String v, String h){
  float x = v.substring(0,3).toFloat() + v.substring(3).toFloat()/60.0;
  if(h=="W") x=-x;
  return x;
}

const char* courseToText(float deg){
  static const char* d[]={"N","NE","E","SE","S","SO","O","NO"};
  return d[(int)((deg+22.5)/45.0) % 8];
}

void parseNMEA(String s){

  if(s.startsWith("$GNGLL")){
    Serial.println(s);
    int p[8],n=0;

    for(int i=0;i<s.length() && n<8;i++)
      if(s[i]==',') p[n++]=i;

    gps_lat = convLat(s.substring(p[0]+1,p[1]), s.substring(p[1]+1,p[2]));
    gps_lon = convLon(s.substring(p[2]+1,p[3]), s.substring(p[3]+1,p[4]));
    gps_fix = s.substring(p[5]+1,p[6])=="A";
  }

  if(s.startsWith("$GNRMC")){
    String t[16];
    int n=0,last=0;

    for(int i=0;i<s.length();i++){
      if(s[i]==','){
        t[n++] = s.substring(last,i);
        last=i+1;
      }
    }

    gps_speed = t[7].toFloat()*1.852;
    gps_course = t[8].toFloat();
  }

  if(s.startsWith("$GPGSV") || s.startsWith("$GNGSV")){
    String t[6];
    int n=0,last=0;

    for(int i=0;i<s.length();i++){
      if(s[i]==','){
        t[n++] = s.substring(last,i);
        last=i+1;
        if(n>=4) break;
      }
    }

    gps_sats = t[3].toInt();
    Serial.print("Satelites GPS: ");
    Serial.println(gps_sats);
    Serial.print("LAT GPS: ");
    Serial.println(gps_lat);
    Serial.print("LONG GPS: ");
    Serial.println(gps_lon);
    
  }
}

void gpsTask(void *p){

  while(true){

    while(GPS.available()){

      char c = GPS.read();

      if(c=='\n'){
        parseNMEA(line);
        line="";
      }
      else if(c!='\r'){
        line += c;
      }
    }

    vTaskDelay(10/portTICK_PERIOD_MS);
  }
}

void initGPS(int rxPin, int txPin, uint32_t baud){

  GPS.begin(baud, SERIAL_8N1, rxPin, txPin);
  Serial.println("Iniciando GPS");

  xTaskCreatePinnedToCore(
    gpsTask,
    "gpsTask",
    4096,
    NULL,
    1,
    NULL,
    1
  );
}