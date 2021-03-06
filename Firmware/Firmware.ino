#include <SPI.h>
#include <MsTimer2.h>

#include "Firmware.h"
#include "config.h"
#include "rfidController.h"
#include "pushButton.h"
#include "ledBehavior.h"
#include "foodHatch.h"
#include "foodMeasurer.h"
#include "serialListener.h"

/* ****************************** GLOABAL VARS ******************************** */

int PROG_FUNC = READY_TO_SERVE;

String currentUID;

unsigned long serveFoodTime = DEFAULT_SERVE_FOOD_TIME;
unsigned long serveFoodClock = 0;

int rfidRestClock = 0;

bool btnPresseted = false;

float foodPercent = 0;
bool updateFootPercet = false;

unsigned long serialCount = 0;


/* ****************************** ARDUINO CORE ******************************** */

void setup() {
    SPI.begin();
    Serial.begin(SERIAL_SPEED);

    MsTimer2::set(INTERRUP_TIME, onInterruptTime);
    MsTimer2::start();

    initSerialListener(onSerialReceive);
    initRfidSensor(RFID_SDA, RFID_RST, onRFIDReceive);
    initPushButton(BTN_PIN, onInterruptBtn, onPressingBtn);
    initFoodHatch(FH_SERVO, FH_SWITCH);
    initFoodSensor(FM_TRIG, FM_ECHO);
    initLedBehavior(LED);
    setLedBehavior(LED_ALIGHT);

    foodPercent = getFoodPercentage(1);
}

void loop() {

    if(PROG_FUNC == SERVING_FOOD) return;
  
    serialListener();
    rfidListener();
    pushButtonListener();
    ledListener();

    if(PROG_FUNC == SERVE_FOOD_FORCE){
        PROG_FUNC = READY_TO_SERVE;
        serve();
    }

    if(updateFootPercet){
        foodPercent = getFoodPercentage(1);
        updateFootPercet = false;
    }
}

/* ******************************** CALLBACKS ********************************* */

// Time
void onInterruptTime(){

    if(PROG_FUNC != READY_TO_SERVE){
        if(serveFoodClock >= serveFoodTime){
            if(PROG_FUNC == DO_NOTHING){
                enableServerFoodMode(true);
                Serial.println("LOG: READY_TO_SERVE");
                return;
            }
        }

        serveFoodClock += INTERRUP_TIME;
    }

    // Da um tempo na leitura RFID
    if(rfidRestClock <= 0){
        rfidRestClock = RFID_COOLDOWN;
        disableRfidReader(false);
    }
    else{
        disableRfidReader(true);
        rfidRestClock -= INTERRUP_TIME;
    }

    updateFootPercet = true;
    
    if(!btnPresseted){

        if(PROG_FUNC == RFID_CANCEL){
            enableRFIDRecordMode(false);
            blinkTimes(3, 50);
            return;
        }

        else if(PROG_FUNC == SERVE_FOOD_OVERRIDE){
            Serial.println("LOG: SERVE_FOOD_OVERRIDE");
            PROG_FUNC = SERVE_FOOD_FORCE;
        }
    }
}

// RFID
void onRFIDReceive(String uid){

    if(PROG_FUNC == RFID_REG){
        currentUID = uid;
        blinkTimes(10, 50);
        rfidRestClock = RFID_COOLDOWN*2;
        disableRfidReader(true);
        Serial.print("LOG: UID registrada ");
        Serial.println(uid);
        enableRFIDRecordMode(false);
        return;
    }

    if(PROG_FUNC == READY_TO_SERVE){
        if(currentUID == uid){
            Serial.println("LOG: READY_TO_SERVE");
            serve();
        }
        else{
            Serial.println("LOG: UDI invalido");
        }
    }
}

// Button
void onInterruptBtn(){
    btnPresseted = false;
}

void onPressingBtn(int pressTime){

    btnPresseted = true;

    if(pressTime >= RFID_REG_TIME){

        if(PROG_FUNC == DO_NOTHING || PROG_FUNC == READY_TO_SERVE || PROG_FUNC == SERVE_FOOD_OVERRIDE){
            enableRFIDRecordMode(true);
        }

        return;
    }

    if(PROG_FUNC == DO_NOTHING || PROG_FUNC == READY_TO_SERVE){
        int f = PROG_FUNC;
        PROG_FUNC = SERVE_FOOD_OVERRIDE;
        setLedBehavior(f == DO_NOTHING ? LED_ALIGHT : LED_OFF);
        return;
    }

    if(PROG_FUNC == RFID_REG){
        PROG_FUNC = RFID_CANCEL;
        setLedBehavior(LED_OFF);
        return;
    }
}

// Serial
void onSerialReceive(byte* data, short int length){

    String result = "NULL";
    if(length > 0){
        switch (data[0]) {

            case SERIAL_STATUS:

                result = "";
                result += PROG_FUNC;
                result += ";";
                result += currentUID + ";";
                result += floatToString(foodPercent);
                result += ";";
                result += serveFoodTime;
                result += ";";
                result += (serveFoodTime - serveFoodClock);

            break;

            case SERIAL_OVERRIDE:

                if(PROG_FUNC == DO_NOTHING || PROG_FUNC == READY_TO_SERVE){
                    PROG_FUNC = SERVE_FOOD_FORCE;
                    setLedBehavior(LED_ALIGHT);
                }

                result = "OK";
            break;

            case SERIAL_SET_TIME:

                serveFoodTime = parseServeFoodTime(data[1]);

                if(length > 2){
                    if(PROG_FUNC == DO_NOTHING || PROG_FUNC == READY_TO_SERVE)
                        enableServerFoodMode(false);
                        serveFoodClock = 0;
                }
                else if(PROG_FUNC == READY_TO_SERVE){
                    serveFoodClock = serveFoodTime;
                }

                if(serveFoodTime < serveFoodClock){
                    serveFoodClock = serveFoodTime;
                    PROG_FUNC == READY_TO_SERVE;
                }

                result = "";
                result += (serveFoodTime - serveFoodClock);

            break;

            case SERIAL_REG_RFID:
                if(PROG_FUNC == DO_NOTHING || PROG_FUNC == READY_TO_SERVE){
                    enableRFIDRecordMode(true);
                    result = "WAITING";
                }
                else if(PROG_FUNC == RFID_REG){
                    enableRFIDRecordMode(false);
                    result = "CANCELED";
                }
            break;

            case SERIAL_SET_RFID:

                result = "";
                for(int i = 1; i < length; i++){
                    Serial.print((char) data[i]);
                    result += (char) data[i];
                }
                Serial.println("");
                currentUID = result;
                if(PROG_FUNC == DO_NOTHING || PROG_FUNC == READY_TO_SERVE){
                    blinkTimes(10, 50);
                }

            break;

        }

        if(result != "NULL"){
            Serial.print(serialCount++);
            Serial.println("S("+result+")");
        }
    }
}

/* ********************************** UTIL *********************************** */

void enableRFIDRecordMode(bool enable){
    if(enable){
        PROG_FUNC = RFID_REG;
        setLedBehavior(LED_BLINKING);
        Serial.println("LOG: RFID_REG");
    }
    else if(PROG_FUNC == RFID_REG || PROG_FUNC == RFID_CANCEL){
        PROG_FUNC = DO_NOTHING;
        setLedBehavior(LED_OFF);
        Serial.println("LOG: RFID_CANCEL");
    }
}

void enableServerFoodMode(bool enable){
    if(enable){
        PROG_FUNC = READY_TO_SERVE;
        serveFoodClock = 0;
        setLedBehavior(LED_ALIGHT);
    }
    else if(PROG_FUNC == READY_TO_SERVE || PROG_FUNC == DO_NOTHING){
        PROG_FUNC = DO_NOTHING;
        setLedBehavior(LED_OFF);
        serveFoodClock = 0;
        Serial.println("LOG: DO_NOTHING");
    }
}

void serve(){
    PROG_FUNC = SERVING_FOOD;
    MsTimer2::stop();
    serveFood(FH_OPEN_TIME, FH_OPENING_SPEED, FH_CLOSING_SPEED);
    PROG_FUNC = READY_TO_SERVE;
    MsTimer2::start();
    enableServerFoodMode(false);
}

char * floatToString(float value){
    char buffer[20];
    return dtostrf(value, 2, 3, buffer);
}

unsigned long parseServeFoodTime (byte data){

    short unit = (data & 0x80) >> 7;
    short t = data & 0x7F;

    unsigned long r = t * 60;   // Tempo em escala de minutos
    if(unit == 1) r *= 60;      // Tempo em escala de horas

    return r * 1000;
}
