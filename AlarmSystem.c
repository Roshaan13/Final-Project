// AlarmSystem_main.c
// Roshaan's portion: RTOS kernel wiring, alarm state machine, system threads
// Runs on TM4C123 TivaC LaunchPad with BoosterPack
// Teammates: Luke (sensors), Tyrece (LCD/buzzer/LED/buttons)

#include <stdint.h>
#include "../inc/BSP.h"
#include "../inc/CortexM.h"
#include "os.h"

#define THREADFREQ 1000  // 1ms time slice (1000 Hz)

// ============================================================
// Alarm State Machine
// ============================================================
typedef enum {
    DISARMED,       // system off, green LED
    ARMED,          // monitoring active, yellow LED
    ALARM_TRIGGERED // intruder detected, red LED + buzzer
} AlarmState_t;

volatile AlarmState_t AlarmState = DISARMED;

// ============================================================
// Semaphores & Shared Variables
// ============================================================
int32_t LCDmutex;        // mutex for LCD access (Tyrece's tasks need this)
int32_t AlarmStateMutex; // protects AlarmState reads/writes

// Mailbox carries sensor alert codes from Luke's sensor tasks
// Codes: 0 = no event, 1 = motion detected, 2 = sound detected
// (Luke's periodic event threads call OS_MailBox_Send())

// ============================================================
// AlarmController — YOUR main thread
// Implements the alarm state machine
// Scheduled round-robin by the RTOS
// ============================================================
void AlarmController(void) {
    uint32_t sensorEvent;

    while (1) {
        sensorEvent = OS_MailBox_Recv(); // blocks until Luke's sensor task sends data

        OS_Wait(&AlarmStateMutex);

        switch (AlarmState) {

            case DISARMED:
                // Ignore all sensor events while disarmed
                // State transitions handled by Tyrece's button task via OS_Signal
                break;

            case ARMED:
                if (sensorEvent == 1 || sensorEvent == 2) {
                    // Intruder detected — trigger alarm
                    AlarmState = ALARM_TRIGGERED;
                    // Tyrece's LED/buzzer task will observe state and react
                }
                break;

            case ALARM_TRIGGERED:
                // Stay triggered until user disarms via button (Tyrece's task)
                // Nothing to do here — just hold state
                break;
        }

        OS_Signal(&AlarmStateMutex);
    }
}
// ============================================================
// ButtonHandler (Tyrece)
// ============================================================
void ButtonHandler(void){
    static uint8_t prev1 = 0, prev2 = 0; 
    // remember last button state so we only trigger once per press

    uint8_t current;

    while(1){

        // ===== BUTTON 1 =====
        current = BSP_Button1_Input();

        // if button just got pressed (was not pressed before)
        if((current == 0) && (prev1 != 0)){

            OS_Wait(&AlarmStateMutex); 
            // lock so no other task changes AlarmState at same time

            // switch between ON and OFF
            if(AlarmState == DISARMED){
                AlarmState = ARMED;
            } else {
                AlarmState = DISARMED;
            }

            OS_Signal(&AlarmStateMutex); 
            // unlock after changing state

            BSP_Buzzer_Set(512); 
            // quick beep so user knows button worked
        }

        prev1 = current; // save button state for next loop

        // ===== BUTTON 2 =====
        current = BSP_Button2_Input();

        // if button just got pressed
        if((current == 0) && (prev2 != 0)){

            OS_Wait(&AlarmStateMutex);

            AlarmState = DISARMED; 
            // force system OFF no matter what

            OS_Signal(&AlarmStateMutex);

            BSP_Buzzer_Set(512); // beep for feedback
        }

        prev2 = current;

        BSP_Delay1ms(50); 
        // small delay so one press doesn't count multiple times
    }
}
// ============================================================
// DisplayTask (Tyrece)
// ============================================================
void DisplayTask(void){

    while(1){

        OS_Wait(&AlarmStateMutex); 
        // grab current system state safely

        // ===== LED + BUZZER =====
        if(AlarmState == DISARMED){
            BSP_RGB_Set(0,500,0);   // green = system off
            BSP_Buzzer_Set(0);      // no sound
        }
        else if(AlarmState == ARMED){
            BSP_RGB_Set(500,500,0); // yellow = system ready
            BSP_Buzzer_Set(0);
        }
        else if(AlarmState == ALARM_TRIGGERED){
            BSP_RGB_Set(500,0,0);   // red = alarm
            BSP_Buzzer_Set(512);    // make noise
        }

        // ===== LCD =====
        OS_Wait(&LCDmutex); 
        // make sure only this task writes to screen

        if(AlarmState == DISARMED){
            BSP_LCD_DrawString(0,0,"SYSTEM: DISARMED", LCD_WHITE);
        }
        else if(AlarmState == ARMED){
            BSP_LCD_DrawString(0,0,"SYSTEM: ARMED   ", LCD_WHITE);
        }
        else{
            BSP_LCD_DrawString(0,0,"!!! ALARM !!!   ", LCD_WHITE);
        }

        OS_Signal(&LCDmutex); 
        // done using screen

        OS_Signal(&AlarmStateMutex); 
        // done using shared state

        // slow things down so screen doesn't flicker
        for(volatile int i=0; i<500000; i++);
    }
}
// ============================================================
// Dummy placeholder — used if a thread slot needs filling
// during early testing (mirrors the lab's Dummy approach)
// ============================================================
void Dummy(void) {
    while (1) {}
}

// ============================================================
// main()
// ============================================================
int main(void) {
    OS_Init();           // disable interrupts, max clock speed
    BSP_LCD_Init();
    BSP_LCD_FillScreen(BSP_LCD_Color565(0, 0, 0));
    BSP_RGB_Init(0, 0, 0);
    BSP_Buzzer_Init(0);
    BSP_Button1_Init();
    BSP_Button2_Init();

    // Initialize semaphores
    OS_InitSemaphore(&LCDmutex, 1);         // 1 = LCD free
    OS_InitSemaphore(&AlarmStateMutex, 1);  // 1 = state unlocked

    // Initialize mailbox (Luke's sensor tasks -> AlarmController)
    OS_MailBox_Init();

    // Add periodic event threads (Luke writes these — placeholders for now)
    // Thread1: motion sensor @ 10 Hz (every 100ms)
    // Thread2: microphone  @ 100 Hz (every 10ms)
    // Replace Dummy() with Luke's actual functions when ready
    OS_AddPeriodicEventThreads(&Dummy, 100, &Dummy, 10);

    // Add 4 main round-robin threads:
    // Thread 0: AlarmController (yours)
    // Thread 1: ButtonHandler   (Tyrece's — arm/disarm)
    // Thread 2: DisplayTask     (Tyrece's — LCD)
    // Thread 3: Dummy / spare
    // Replace Dummy() below with Tyrece's actual functions when ready
    OS_AddThreads(&AlarmController, &ButtonHandler, &DisplayTask, &Dummy);

    OS_Launch(BSP_Clock_GetFreq() / THREADFREQ); // start RTOS, never returns
    return 0;
}
