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
// TEMP TEST SENSOR EVENTS
// ============================================================
// These are temporary replacements for Luke's real sensor code.
// They let the board actually trigger the alarm during testing.
//
// IMPORTANT:
// Right now the old code used Dummy() for both periodic event threads,
// which meant the alarm could never trigger from any sensor event.
//
// Once Luke finishes the real motion and microphone code,
// replace these bodies with real sensor reads and threshold checks,
// or replace these function names entirely with Luke's functions.
// ============================================================

// Temporary motion event thread
// Sends a "motion detected" event into the mailbox
void MotionSensorEvent(void){
    OS_MailBox_Send(1);
}

// Temporary microphone event thread
// Right now this does nothing on purpose so only one fake sensor
// triggers during testing. You can later change this to:
// OS_MailBox_Send(2);
void MicrophoneEvent(void){
    // placeholder for Luke's real microphone logic
}

// ============================================================
// AlarmController — YOUR main thread
// Implements the alarm state machine
// Scheduled round-robin by the RTOS
// ============================================================
void AlarmController(void) {
    uint32_t sensorEvent;

    while(1){
        sensorEvent = OS_MailBox_Recv(); // blocks until Luke's sensor task sends data

        OS_Wait(&AlarmStateMutex);

        switch (AlarmState) {

            case DISARMED:
                // Ignore all sensor events while disarmed
                // State transitions handled by Tyrece's button task
                break;

            case ARMED:
                if (sensorEvent == 1 || sensorEvent == 2) {
                    // Intruder detected — trigger alarm
                    AlarmState = ALARM_TRIGGERED;
                    // Tyrece's LED/buzzer/LCD task will observe state and react
                }
                break;

            case ALARM_TRIGGERED:
                // Stay triggered until user disarms via button
                // Nothing to do here — just hold state
                break;
        }

        OS_Signal(&AlarmStateMutex);
    }
}

// ============================================================
// ButtonHandler (Tyrece)
// ============================================================
// UPDATED:
// - cleaned up button logic slightly
// - removed buzzer writes from this task
// Why?
// Because DisplayTask should be the one place that controls
// alarm buzzer behavior. This avoids tasks fighting over buzzer state.
//
// ASSUMPTION:
// pressed = 0, released = 1
// This matches common Tiva button logic.
// ============================================================
void ButtonHandler(void){
    uint8_t prev1, prev2;        // stores previous button states
    uint8_t current1, current2;  // stores current button states

    // Read the initial state of both buttons
    // BSP_ButtonX_Input() returns:
    //   nonzero = button not pressed
    //   0       = button pressed
    // We convert that into:
    //   1 = unpressed
    //   0 = pressed
    prev1 = (BSP_Button1_Input() != 0);
    prev2 = (BSP_Button2_Input() != 0);

    while(1){

        // Read current button states each time through the loop
        // This keeps the values simple for edge detection
        current1 = (BSP_Button1_Input() != 0);  // Button 1: 1 = unpressed, 0 = pressed
        current2 = (BSP_Button2_Input() != 0);  // Button 2: 1 = unpressed, 0 = pressed

        // Detect a new press on Button 1
        // Condition means:
        //   current1 == 0  -> button is pressed now
        //   prev1 == 1     -> button was not pressed before
        // So this catches the transition from unpressed to pressed
        if((current1 == 0) && (prev1 == 1)){

            // Lock shared alarm state so no other task changes it at the same time
            OS_Wait(&AlarmStateMutex);

            // Button 1 toggles between DISARMED and ARMED
            if(AlarmState == DISARMED){
                AlarmState = ARMED;
            } else {
                AlarmState = DISARMED;
            }

            // Release the mutex after updating the shared state
            OS_Signal(&AlarmStateMutex);
        }

        // Detect a new press on Button 2
        // Button 2 always forces the system into DISARMED
        if((current2 == 0) && (prev2 == 1)){

            // Lock shared alarm state before changing it
            OS_Wait(&AlarmStateMutex);

            // Force alarm system to DISARMED
            AlarmState = DISARMED;

            // Release the mutex
            OS_Signal(&AlarmStateMutex);
        }

        // Save current states so they become the "previous" states
        // on the next loop iteration
        prev1 = current1;
        prev2 = current2;

        // Small delay for button debounce
        // This helps prevent one press from being counted multiple times
        BSP_Delay1ms(50);
    }
}

// ============================================================
// DisplayTask (Tyrece)
// ============================================================
// UPDATED:
// The original version kept AlarmStateMutex locked while updating
// LED, buzzer, and LCD. That works, but it holds the shared state
// lock longer than necessary.
//
// NEW APPROACH:
// 1. lock AlarmStateMutex
// 2. copy AlarmState into a local variable
// 3. unlock AlarmStateMutex quickly
// 4. update hardware using the local copy
//
// This is cleaner and safer for multitasking.
// ============================================================
void DisplayTask(void){
    AlarmState_t localState;  
    // NEW:
    // local copy of the shared alarm state

    while(1){

        OS_Wait(&AlarmStateMutex); 
        // grab current system state safely

        localState = AlarmState;  
        // copy shared state quickly

        OS_Signal(&AlarmStateMutex); 
        // release lock immediately
        // hardware updates below now use the local copy

        // ===== LED + BUZZER =====
        if(localState == DISARMED){
            BSP_RGB_Set(0,500,0);   // green = system off
            BSP_Buzzer_Set(0);      // no sound
        }
        else if(localState == ARMED){
            BSP_RGB_Set(500,500,0); // yellow = system ready
            BSP_Buzzer_Set(0);
        }
        else if(localState == ALARM_TRIGGERED){
            BSP_RGB_Set(500,0,0);   // red = alarm
            BSP_Buzzer_Set(512);    // make noise
        }

        // ===== LCD =====
        OS_Wait(&LCDmutex); 
        // make sure only this task writes to screen

        if(localState == DISARMED){
            BSP_LCD_DrawString(0,0,"SYSTEM: DISARMED", LCD_WHITE);
        }
        else if(localState == ARMED){
            BSP_LCD_DrawString(0,0,"SYSTEM: ARMED   ", LCD_WHITE);
        }
        else{
            BSP_LCD_DrawString(0,0,"!!! ALARM !!!   ", LCD_WHITE);
        }

        OS_Signal(&LCDmutex); 
        // done using screen

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

    // IMPORTANT:
    // These are the LaunchPad button init calls.
    // If your project is supposed to use BoosterPack buttons instead,
    // these may need to be replaced with the correct BoosterPack driver calls.
    BSP_Button1_Init();
    BSP_Button2_Init();

    // Initialize semaphores
    OS_InitSemaphore(&LCDmutex, 1);         // 1 = LCD free
    OS_InitSemaphore(&AlarmStateMutex, 1);  // 1 = state unlocked

    // Initialize mailbox (Luke's sensor tasks -> AlarmController)
    OS_MailBox_Init();

    // Add periodic event threads
    //
    // OLD VERSION:
    // OS_AddPeriodicEventThreads(&Dummy, 100, &Dummy, 10);
    //
    // PROBLEM:
    // Dummy() never sends mailbox data, so AlarmController never receives
    // any sensor event and the alarm can never trigger automatically.
    //
    // NEW VERSION:
    // Use temporary test sensor functions so the state machine can be tested now.
    //
    // MotionSensorEvent runs every 1000 ticks here for easier testing.
    // MicrophoneEvent is still a placeholder for now.
    //
    // LATER:
    // Replace these with Luke's real sensor event threads and real periods.
    OS_AddPeriodicEventThreads(&MotionSensorEvent, 1000, &MicrophoneEvent, 1000);

    // Add 4 main round-robin threads:
    // Thread 0: AlarmController (yours)
    // Thread 1: ButtonHandler   (Tyrece's — arm/disarm)
    // Thread 2: DisplayTask     (Tyrece's — LCD/LED/buzzer)
    // Thread 3: Dummy / spare
    OS_AddThreads(&AlarmController, &ButtonHandler, &DisplayTask, &Dummy);

    OS_Launch(BSP_Clock_GetFreq() / THREADFREQ); // start RTOS, never returns
    return 0;
}
