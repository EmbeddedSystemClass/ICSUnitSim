/*
Interactive ICS process simulation with ModbusTCP
By Zakir Supeyev 2020 zakir.supeyev@gmail.com

https://github.com/akiUp/ICSUnitSim

//-----Modbus Coils Offsets (read\write)
mot_start, address: 100, boolean, Start button, 1 to activate, returns to 0 after activation
mot_stop, address: 110, boolean, Stop button, 1 to activate, returns to 0 after activation
valve_state, address: 120, boolean 0-Closed 1-Open (Controlled by start/stop logic)
//-----Modbus input status (Discrete Inputs) Offsets(read only)
mot_run, address: 100, boolean, 1 running, 0 stop
mot_trip, address: 110, boolean, Motor trip state, cause: Tank full, Valve closed, Random trip (1 of 10000)
//-----Modbus Input Register Offsets (read only)
mot_load, address: 100; Uint16, range:0-100, random motor load simulation
flowm_flow, address: 110, Uint16, range:0-10, flow value depending on mot_load 
tank_level, address: 120, Uint16, range:0-65000, 65000 value trips the motor, has constant (random of 4) drainage flow

Based on Alexander Emelianov (a.m.emelianov@gmail.com) Modbus library
  https://github.com/emelianov/modbus-esp8266
*/
#define Debug //Comment this field if debug output via serial is not required
#ifdef ESP8266
 #include <ESP8266WiFi.h>
#else //ESP32
 #include <WiFi.h>
#endif
#include <ModbusIP_ESP8266.h>

//Wifi Variables
const char *ssid = "YourSSIDHere";
const char *password = "YourPSKHere";

// -----Process variables
//Motor variables

int mot_load =0; //load of the motor
bool mot_trip = false; //motor tripping state variable
bool mot_run = false; //motor "RUN" state variable
int mot_randerr =0; //motor random error

//Flow meter variable
int flowm_flow =0;
int flowm_max =4; //Maximum flow 
//Tank level variable
int tank_level = 0; //Tank level 0=empty
const int tank_HH = 65000; //tank maximum capacity (HiHi alarm)


//-----Modbus Coils Offsets (read\write)
const int mot_start_C = 100;
const int mot_stop_C = 110;
const int valve_state_C = 120;
//-----Modbus input status (Discrete Inputs) Offsets(read only)
const int mot_run_IS = 100;
const int mot_trip_IS = 110;
//-----Modbus Input Register Offsets (read only)
const int mot_load_IR = 100;
const int flowm_flow_IR = 110;
const int tank_level_IR = 120;

//Used Pins
const int runPin = 2; //Used to signal RUN state

//ModbusIP object
ModbusIP modbus;
  
void setup() {
 #ifdef ESP8266
  Serial.begin(74880);
 #else
  Serial.begin(115200);
 #endif
 
  WiFi.begin(ssid, password);
  
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
 
  Serial.println("");
  Serial.println("WiFi connected");  
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());

  modbus.slave();

  pinMode(runPin, OUTPUT);
  //Define coils
  modbus.addCoil(mot_start_C, false);
  modbus.addCoil(mot_stop_C, false);
  modbus.addCoil(valve_state_C, false);
  //define Discrete Inputs
  modbus.addIsts(mot_run_IS, false);
  modbus.addIsts(mot_trip_IS, false);
  //Define Input registers
  modbus.addIreg(mot_load_IR, 0);
  modbus.addIreg(flowm_flow_IR, 0);
  modbus.addIreg(tank_level_IR, 0);
}
 
void loop() {
   //Call once inside loop() - modbus engine
   modbus.task();
 
   if (mot_run==true) //-----checking running state and running process if true
   {
    digitalWrite(runPin, mot_run); //turn RUN led on
    modbus.Coil(mot_start_C, false); //reset start signal to 0
    mot_randerr = random (10000); // random error function for motor
    mot_load = 60+random (4)*10; // simulate random motor load 
    flowm_flow = mot_load/10; // simulate flow dependency on motor load
    tank_level = tank_level+flowm_flow; //simulate tank filling dependency from flow
          
        if (mot_randerr == 10000) //motor random trip condition 
        {
          #ifdef Debug
          Serial.println ("Random trip!");
          #endif
          flowm_flow = 0; //no flow due to motor trip
          mot_load= 0; //no load due to motor trip
          mot_run = 0; //Reset RUN condition
          mot_trip = true; //motor trip bit on
          modbus.Coil(valve_state_C, false);//fail-close valve simulation
          digitalWrite(runPin, mot_run); //turn RUN led off
        }
        
        if (modbus.Coil(valve_state_C) == false) //tripping motor on valve state
        {
          #ifdef Debug
          Serial.println ("Valve trip!");
          #endif
          flowm_flow = 0; //no flow due to motor trip
          mot_load= 0; //no load due to motor trip
          mot_run = 0; //Reset RUN condition
          mot_trip = true; //motor trip bit on
          modbus.Coil(valve_state_C, false);//fail-close valve simulation
          digitalWrite(runPin, mot_run); //turn RUN led off
         }
         
        if (tank_level>tank_HH) //tripping on full condition(HiHi trip)
        {
          #ifdef Debug
          Serial.println ("Tank full trip!");
          #endif
          flowm_flow = 0; //no flow due to motor trip
          mot_load= 0; //no load due to motor trip
          mot_run = 0; //Reset RUN condition
          mot_trip = true; //motor trip bit on
          digitalWrite(runPin, mot_run); //turn RUN led off
         }

          if (modbus.Coil(mot_stop_C)==true) //-----Stop signal routine
        {
          mot_run = !(modbus.Coil(mot_stop_C)); //stopping motor
          digitalWrite(runPin, mot_run); //turn RUN led off
          flowm_flow = 0; //no flow due to motor stop
          mot_load= 0; //no load due to motor stop
          modbus.Coil(valve_state_C, false);//automated valve closing routine
          modbus.Coil(mot_stop_C, false); //reset stop signal to 0
        } 
         
   } 
   else   //wrapper in case of motor not running
   {
      if (modbus.Coil(mot_start_C) == true) //motor starting routine
        {
        modbus.Coil(mot_stop_C, false); //reset stop signal to 0 if set to avoid failstart
        mot_run = modbus.Coil(mot_start_C); //assign start coil status to RUN state
        digitalWrite(runPin, mot_run); //turn RUN led on 
        mot_trip = false; //reset error bit in case of error shutdown
        modbus.Coil(valve_state_C, true); //trigger opening valve
        modbus.Ists(mot_run_IS, mot_run);//update RUN status coil
        }
   } 
  
   //placehodler for tank drain logic
   if (tank_level>6) tank_level=tank_level-random(6); //constant random drain if tank is not empty;
   //assign read-only registers and inputs per iteration
   modbus.Ists(mot_trip_IS, mot_trip);
   modbus.Ists(mot_run_IS, mot_run);
   modbus.Ireg(mot_load_IR, mot_load);
   modbus.Ireg(flowm_flow_IR, flowm_flow);
   modbus.Ireg(tank_level_IR, tank_level);
      #ifdef Debug //Debug output to serial interface
      Serial.print ("Motor RUN: ");
      Serial.print (modbus.Ists(mot_run_IS));
      Serial.print (" Motor trip: ");
      Serial.print (modbus.Ists(mot_trip_IS));
      Serial.print (" Valve: ");
      Serial.print (modbus.Coil(valve_state_C));
      Serial.print (" Flow: ");
      Serial.print (modbus.Ireg(flowm_flow_IR));
      Serial.print (" Load: ");
      Serial.print (modbus.Ireg(mot_load_IR));
      Serial.print (" Tank: ");
      Serial.print (modbus.Ireg(tank_level_IR));  
      Serial.print (" random Trip value: ");
      Serial.println (mot_randerr);
      #endif
   
 delay(100);
}
