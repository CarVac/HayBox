#include "comms/GamecubeBackend.hpp"

#include "core/InputSource.hpp"

#include "modes/MeleeLimits.hpp"

#include <GamecubeConsole.hpp>
#include <hardware/pio.h>
#include <hardware/timer.h>
#include <hardware/gpio.h>

//#define TIMINGDEBUG

GamecubeBackend::GamecubeBackend(
    InputSource **input_sources,
    size_t input_source_count,
    uint data_pin,
    bool nerfOn,
    PIO pio,
    int sm,
    int offset
)
    : CommunicationBackend(input_sources, input_source_count) {
    _gamecube = new GamecubeConsole(data_pin, pio, sm, offset);
    _report = default_gc_report;
    _nerfOn = nerfOn;
}

GamecubeBackend::~GamecubeBackend() {
    delete _gamecube;
}

void GamecubeBackend::SendReport() {
    // Update slower inputs before we start waiting for poll.
    //ScanInputs(InputScanSpeed::SLOW);
    //ScanInputs(InputScanSpeed::MEDIUM);
    //This fork won't support slower inputs

    static uint32_t minLoop = 16384;
    static uint loopCount = 0;
    static bool detect = true;//false means run
    static uint sampleCount = 1;
    static uint32_t sampleSpacing = 0;
    static uint32_t oldSampleTime = 0;
    static uint32_t newSampleTime = 0;
    static uint32_t loopTime = 0;
    //const uint fastestLoop = 950; //fastest possible loop for AVR
    const uint fastestLoop = 450; //fastest possible loop for rp2040

    oldSampleTime = newSampleTime;
    //YOU CANNOT USE MICROS ON AVR
    //You need to manually use the low level timers
    newSampleTime = micros();
    loopTime = newSampleTime - oldSampleTime;

    if(detect) {
        //run loop time detection procedure
#ifdef TIMINGDEBUG
        gpio_put(1, loopCount%2 == 0);
#endif
        loopCount++;
        if(loopCount > 5 && loopTime > 300) {//screen out implausibly fast samples; the limit is 2500 Hz (400 us)
            minLoop = min(minLoop, loopTime);
        }
        if(loopCount >= 100) {
            detect = false;
            sampleCount = 1;
            while(1000*sampleCount <= minLoop) {//we want [sampleCount] ms-spaced samples within the smallest possible loop
                sampleCount++;
            }
            sampleSpacing = minLoop / sampleCount;
            if(sampleSpacing < fastestLoop && sampleCount > 1) {
                sampleCount--;
                sampleSpacing = minLoop / sampleCount;
            }
        }
        // Make sure to respond while measuring.
        ScanInputs(InputScanSpeed::FAST);

        // Run gamemode logic.
        UpdateOutputs();

        _report.a = _outputs.a;
        _report.b = _outputs.b;
        _report.x = _outputs.x;
        _report.y = _outputs.y;
        _report.z = _outputs.buttonR;
        _report.l = _outputs.triggerLDigital;
        _report.r = _outputs.triggerRDigital;
        _report.start = _outputs.start;
        _report.dpad_left = _outputs.dpadLeft | _outputs.select;
        _report.dpad_right = _outputs.dpadRight | _outputs.home;
        _report.dpad_down = _outputs.dpadDown;
        _report.dpad_up = _outputs.dpadUp;

        // Analog outputs
        _report.stick_x = _outputs.leftStickX;
        _report.stick_y = _outputs.leftStickY;
        _report.cstick_x = _outputs.rightStickX;
        _report.cstick_y = _outputs.rightStickY;
        _report.l_analog = _outputs.triggerLAnalog;
        _report.r_analog = _outputs.triggerRAnalog;
    } else {
        //run the delay procedure based on samplespacing
        //in the stock arduino software, it samples 850 us after the end of the poll response
        //we want the last sample to begin [850 + extra computation time] before the beginning of the last poll to give room for the sample and the travel time+nerf computation
        //
        for (uint i = 0; i < sampleCount; i++) {
#ifdef TIMINGDEBUG
            gpio_put(1, 0);
#endif

            const int nerfTime = 0;
            const int computationTime = 250 + nerfTime;//*_nerfOn;//us; depends on the platform.
            const uint32_t targetTime = ((i+1)*sampleSpacing)-computationTime;
            int count = 0;
            while(micros() - newSampleTime < targetTime) {
                count++;//do something?
                //spinlock
            }
#ifdef TIMINGDEBUG
            gpio_put(1, count>0);
#endif

            ScanInputs(InputScanSpeed::FAST);

            // Run gamemode logic.
            UpdateOutputs();

            if(_gamemode->isMelee()) {
                //APPLY NERFS HERE
                OutputState nerfedOutputs;
                limitOutputs(sampleSpacing/4, _nerfOn ? AB_A : AB_B, _inputs, _outputs, nerfedOutputs);

                // Digital outputs
                _report.a = nerfedOutputs.a;
                _report.b = nerfedOutputs.b;
                _report.x = nerfedOutputs.x;
                _report.y = nerfedOutputs.y;
                _report.z = nerfedOutputs.buttonR;
                _report.l = nerfedOutputs.triggerLDigital;
                _report.r = nerfedOutputs.triggerRDigital;
                _report.start = nerfedOutputs.start;
                _report.dpad_left = nerfedOutputs.dpadLeft | nerfedOutputs.select;
                _report.dpad_right = nerfedOutputs.dpadRight | nerfedOutputs.home;
                _report.dpad_down = nerfedOutputs.dpadDown;
                _report.dpad_up = nerfedOutputs.dpadUp;

                // Analog outputs
                _report.stick_x = nerfedOutputs.leftStickX;
                _report.stick_y = nerfedOutputs.leftStickY;
                _report.cstick_x = nerfedOutputs.rightStickX;
                _report.cstick_y = nerfedOutputs.rightStickY;
                _report.l_analog = nerfedOutputs.triggerLAnalog;
                _report.r_analog = nerfedOutputs.triggerRAnalog;
            } else {
                // Digital outputs
                _report.a = _outputs.a;
                _report.b = _outputs.b;
                _report.x = _outputs.x;
                _report.y = _outputs.y;
                _report.z = _outputs.buttonR;
                _report.l = _outputs.triggerLDigital;
                _report.r = _outputs.triggerRDigital;
                _report.start = _outputs.start;
                _report.dpad_left = _outputs.dpadLeft | _outputs.select;
                _report.dpad_right = _outputs.dpadRight | _outputs.home;
                _report.dpad_down = _outputs.dpadDown;
                _report.dpad_up = _outputs.dpadUp;

                // Analog outputs
                _report.stick_x = _outputs.leftStickX;
                _report.stick_y = _outputs.leftStickY;
                _report.cstick_x = _outputs.rightStickX;
                _report.cstick_y = _outputs.rightStickY;
                _report.l_analog = _outputs.triggerLAnalog;
                _report.r_analog = _outputs.triggerRAnalog;
            }
        }
        //if the loop time is greater than 150% expected or lesser than 75% expected
        if(loopTime > minLoop + (minLoop >> 1) || loopTime < minLoop - (minLoop >> 2)) {
            detect = true;//stop scanning inputs briefly and re-measure timings
            loopCount = 0;
            sampleCount = 1;
            sampleSpacing = 0;
            oldSampleTime = 0;
            newSampleTime = 0;
            loopTime = 0;
        }
    }

    _gamecube->WaitForPollStart();
#ifdef TIMINGDEBUG
    gpio_put(1, 1);
#endif

    // Send outputs to console unless poll command is invalid.
    if (_gamecube->WaitForPollEnd() != PollStatus::ERROR) {
        _gamecube->SendReport(&_report);
    }
}

int GamecubeBackend::GetOffset() {
    return _gamecube->GetOffset();
}
