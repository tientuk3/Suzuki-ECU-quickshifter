#pragma keyword asm on
// Link the code to the correct addresses (free sections in the stock ECU binary)
#ifndef UNIT_TEST
    // see Shifter_Release.mak for the addresses
    #pragma SECTION P CODE
    #pragma SECTION C CONST_DATA
    #pragma SECTION B RAM_DATA
#endif

// External variables that exist in ECU RAM outside of this program but are accessed from here
// These are linked to their hardcoded addresses (from 32920-21H50 GSXR 1000 K7)
// Note: if the code were to be ported to other M32R ECUs, these will have to be changed accordingly!
#ifndef UNIT_TEST
    #pragma ADDRESS ECU_RPM                 0x80507A
    #pragma ADDRESS ECU_TPS                 0x80508E
    #pragma ADDRESS ECU_SELECTED_GEAR       0x8050FC
    #pragma ADDRESS ECU_IGN_LIMIT_FLAGS     0x8063E2
    #pragma ADDRESS ECU_TML0_COUNTER        0x8003E0
    #pragma ADDRESS COV3_VOLTAGE            0x8050CA
#endif

volatile unsigned short ECU_RPM;
volatile unsigned char ECU_TPS;
volatile unsigned char ECU_SELECTED_GEAR;
volatile unsigned char ECU_IGN_LIMIT_FLAGS;
volatile unsigned int ECU_TML0_COUNTER;
volatile unsigned char COV3_VOLTAGE;

// Constant data stored in flash, can be changed without compiling the patch again
const volatile unsigned short kill_time_ms = 70u;       // kill time for all gears
const volatile unsigned short cooldown_time_ms = 300u;  // minimum delay between shifts to avoid glitches
const volatile unsigned short min_shift_rpm = 0x1E00u;  // 3000 rpm in ECU units, i.e. scaled up by 2.56
const volatile unsigned char min_shift_tps = 0x0u;      // minimum TPS position to allow shifts

// Ignition limiter handling function to which the call is patched
const unsigned int ignition_limiter_func = 0x03B7A8u;

#define SIGNAL_ACTIVE (COV3_VOLTAGE < 0x10u)            // true when sensor signals the ECU to shift
#define LIMIT_ENABLE_MASK 0x3u
#define IGN_LIMIT_BIT (ECU_IGN_LIMIT_FLAGS & LIMIT_ENABLE_MASK)
#define SHIFTER_READY 0x1u 
#define SHIFTER_ACTIVE 0x2u
#define SHIFTER_COOLDOWN 0x4u
#define MS_TO_TICKS(time_ms) (10000u * time_ms) // Timer runs on 10 MHz clock

// Patch local variables
unsigned char initialized;
unsigned char shifter_status;
unsigned int kill_target_duration; // in timer ticks
unsigned int kill_time_start;
unsigned int cooldown_time_start;

void shifter() {

#ifndef UNIT_TEST
    /*
    This function is entered by patching the call to the ignition limiter function
    to call here instead. Now we need to call that function as the first thing, since
    the shifter operations need to happen (immediately) after it.

    The reason to do this is that ignition_limiter_func does not have empty space at the end.
    */
    ((void(*)(void))ignition_limiter_func)();
#endif

    if (initialized != 1) {
        // initialization
        shifter_status = SHIFTER_READY;
        kill_target_duration = 0;
        kill_time_start = 0;
        initialized = 1;
        cooldown_time_start = 0;
    }

    if (shifter_status == SHIFTER_READY) { // currently not active
        if (SIGNAL_ACTIVE) {
            // check criteria
            if (ECU_RPM > min_shift_rpm && ECU_SELECTED_GEAR < 6 && ECU_TPS >= min_shift_tps) {
                shifter_status = SHIFTER_ACTIVE;
                kill_time_start = ECU_TML0_COUNTER;
                kill_target_duration = MS_TO_TICKS(kill_time_ms);
            } else {
                shifter_status = SHIFTER_COOLDOWN;
                cooldown_time_start = ECU_TML0_COUNTER;
            }
        }
    }

    if (shifter_status == SHIFTER_ACTIVE) { // currently active
        ECU_IGN_LIMIT_FLAGS |= LIMIT_ENABLE_MASK;

        if ((ECU_TML0_COUNTER - kill_time_start) > kill_target_duration) {
            // deactivate shifter
            ECU_IGN_LIMIT_FLAGS &= (~LIMIT_ENABLE_MASK);
            shifter_status = SHIFTER_COOLDOWN;
            kill_time_start = 0;
            kill_target_duration = 0;
            cooldown_time_start = ECU_TML0_COUNTER;
        }
    }

    if (shifter_status == SHIFTER_COOLDOWN) { // currently active
        if ((ECU_TML0_COUNTER - cooldown_time_start) > MS_TO_TICKS(cooldown_time_ms)) {
            // transition to "ready" state again
            shifter_status = SHIFTER_READY;
            cooldown_time_start = 0;
        }
    }
}
