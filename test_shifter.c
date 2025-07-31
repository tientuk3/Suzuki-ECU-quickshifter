#include "assert.h"
#include "stdio.h"
#include "limits.h"

#define UNIT_TEST

#include "shifter.c"

void reset() {
    initialized = 0;
    COV3_VOLTAGE = 0xFF;
    ECU_RPM = 0;
    ECU_SELECTED_GEAR = 0;
    ECU_IGN_LIMIT_FLAGS = 0;
    ECU_TML0_COUNTER = 0;
}

int main() {
    // initialization
    reset();    
    shifter();
    assert(initialized == 1);
    assert(shifter_status == SHIFTER_READY);

    // if RPM condition not met, shifter goes to cooldown mode and returns to "ready", without activating the kill
    reset();
    COV3_VOLTAGE = 0;
    ECU_RPM = 0x100;
    ECU_SELECTED_GEAR = 2;
    shifter();
    assert(initialized == 1);
    assert(shifter_status == SHIFTER_COOLDOWN);
    ECU_TML0_COUNTER += MS_TO_TICKS(cooldown_time_ms) / 2;
    shifter();
    assert(!IGN_LIMIT_BIT);
    assert(shifter_status == SHIFTER_COOLDOWN);
    ECU_TML0_COUNTER += MS_TO_TICKS(cooldown_time_ms);
    shifter();
    assert(shifter_status == SHIFTER_READY);

    // timer wrapping around during cooldown is handled properly
    reset();
    COV3_VOLTAGE = 0;
    ECU_RPM = 0x100;
    ECU_SELECTED_GEAR = 2;
    ECU_TML0_COUNTER = UINT_MAX - 100;
    shifter();
    assert(initialized == 1);
    assert(shifter_status == SHIFTER_COOLDOWN);
    ECU_TML0_COUNTER += MS_TO_TICKS(cooldown_time_ms) / 2;
    shifter();
    assert(!IGN_LIMIT_BIT);
    assert(shifter_status == SHIFTER_COOLDOWN);
    ECU_TML0_COUNTER += MS_TO_TICKS(cooldown_time_ms);
    shifter();
    assert(shifter_status == SHIFTER_READY);

    // if gear condition not met, shifter goes to cooldown mode and returns to "ready", without activating the kill
    reset();
    COV3_VOLTAGE = 0;
    ECU_RPM = 0x4000;
    ECU_SELECTED_GEAR = 6;
    shifter();
    assert(initialized == 1);
    assert(shifter_status == SHIFTER_COOLDOWN);

    // if conditions are met, shifter activates, kills the ignition, and returns to cooldown -> ready state
    reset();
    COV3_VOLTAGE = 0;
    ECU_RPM = 0x4000;
    ECU_SELECTED_GEAR = 2;
    shifter();
    assert(initialized == 1);
    assert(shifter_status == SHIFTER_ACTIVE);
    assert(IGN_LIMIT_BIT);
    ECU_TML0_COUNTER += kill_target_duration / 2;
    shifter();
    assert(shifter_status == SHIFTER_ACTIVE);
    assert(IGN_LIMIT_BIT);
    ECU_TML0_COUNTER += kill_target_duration;
    shifter();
    assert(shifter_status == SHIFTER_COOLDOWN);
    ECU_TML0_COUNTER += MS_TO_TICKS(cooldown_time_ms) / 2;
    ECU_IGN_LIMIT_FLAGS &= ~LIMIT_ENABLE_MASK;
    shifter();
    assert(!IGN_LIMIT_BIT);
    assert(shifter_status == SHIFTER_COOLDOWN);
    ECU_TML0_COUNTER += MS_TO_TICKS(cooldown_time_ms);
    ECU_IGN_LIMIT_FLAGS &= ~LIMIT_ENABLE_MASK;
    shifter();
    assert(!IGN_LIMIT_BIT);
    assert(shifter_status == SHIFTER_READY);

    printf("Tests passed \n");
    return 0;
}