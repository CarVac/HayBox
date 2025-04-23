#include "comms/backend_init.hpp"
#include "config_defaults.hpp"
#include "core/CommunicationBackend.hpp"
#include "core/KeyboardMode.hpp"
#include "core/Persistence.hpp"
#include "core/mode_selection.hpp"
#include "core/pinout.hpp"
#include "core/state.hpp"
#include "input/DebouncedGpioButtonInput.hpp"
#include "input/NunchukInput.hpp"
#include "reboot.hpp"
#include "stdlib.hpp"

#include <config.pb.h>

Config config = default_config;

GpioButtonMapping button_mappings[] = {
    {BTN_LF1,  21 },
    { BTN_LF2, 26 },
    { BTN_LF3, 22 },
    { BTN_LF4, 21 },

    { BTN_LT1, 20 },
    { BTN_LT2, 19 },

    { BTN_MB1, 28 },
    { BTN_MB2, 18},
    { BTN_MB3, 17},

    { BTN_RT1, 12},
    { BTN_RT2, 13},
    { BTN_RT3, 10},
    { BTN_RT4, 9},
    { BTN_RT5, 11},

    { BTN_RF1, 2},
    { BTN_RF2, 4},
    { BTN_RF3, 6},
    { BTN_RF4, 8},

    { BTN_RF5, 1},
    { BTN_RF6, 2},
    { BTN_RF7, 7},
    { BTN_RF8, 5},
};
const size_t button_count = sizeof(button_mappings) / sizeof(GpioButtonMapping);

DebouncedGpioButtonInput<button_count> gpio_input(button_mappings);

const Pinout pinout = {
    .joybus_data = 28,
    .mux = -1,
    .nunchuk_detect = -1,
    .nunchuk_sda = -1,
    .nunchuk_scl = -1,
};

CommunicationBackend **backends = nullptr;
size_t backend_count;
KeyboardMode *current_kb_mode = nullptr;

void setup() {
    static InputState inputs;

    // Create GPIO input source and use it to read button states for checking button holds.
    gpio_input.UpdateInputs(inputs);

    // Check bootsel button hold as early as possible for safety.
    if (inputs.mb1) {
        reboot_bootloader();
    }

    // Turn on LED to indicate firmware booted.
    gpio_init(PICO_DEFAULT_LED_PIN);
    gpio_set_dir(PICO_DEFAULT_LED_PIN, GPIO_OUT);
    gpio_put(PICO_DEFAULT_LED_PIN, 1);

    // Attempt to load config, or write default config to flash if failed to load config.
    if (!persistence.LoadConfig(config)) {
        persistence.SaveConfig(config);
    }

    // Create array of input sources to be used.
    static InputSource *input_sources[] = {};
    size_t input_source_count = sizeof(input_sources) / sizeof(InputSource *);

    backend_count =
        initialize_backends(backends, inputs, input_sources, input_source_count, config, pinout);

    setup_mode_activation_bindings(config.game_mode_configs, config.game_mode_configs_count);
}

void loop() {
    select_mode(backends, backend_count, config);

    for (size_t i = 0; i < backend_count; i++) {
        backends[i]->SendReport();
    }

    if (current_kb_mode != nullptr) {
        current_kb_mode->SendReport(backends[0]->GetInputs());
    }
}

/* Button inputs are read from the second core */

void setup1() {
    while (backends == nullptr) {
        tight_loop_contents();
    }
}

void loop1() {
    if (backends != nullptr) {
        gpio_input.UpdateInputs(backends[0]->GetInputs());
        bool isMelee = false;
        for (size_t i = 0; i < backend_count; i++) {
            if(backends[i]->isMelee()) {
                isMelee = true;
            }
        }
        if (isMelee) {
            //get exactly 2 khz input scanning
            const uint32_t interval = 500;//microseconds
            const uint32_t quarterInterval = interval/4;//unit of 4 microseconds
            const uint32_t beforeMicros = micros();
            uint32_t afterMicros = beforeMicros;
            while ((afterMicros - beforeMicros) < interval) {
                afterMicros = micros();
            }

            gpio_input.UpdateInputs(backends[0]->GetInputs());
            for (size_t i = 0; i < backend_count; i++) {
                backends[i]->ScanInputs();
                /*
                //begin timing macro
                //times given in half millisecond intervals
                const uint32_t frame = 16;
                const uint32_t hold = 8;
                static uint32_t counter = 0;
                static uint32_t test = 0;
                if((backends[i]->_inputs.lt1 && backends[i]->_inputs.rf7) || test == 1) { //modx and lightshield
                    test = 1;
                    const uint32_t loop = 6*frame;
                    if(counter % loop <= 6*hold) {
                        backends[i]->_inputs.lf1 = true;
                    } else {
                        backends[i]->_inputs.lf1 = false;
                    }
                    backends[i]->_inputs.lt1 = false;
                    backends[i]->_inputs.rf7 = false;
                    counter++;
                    if(counter > 2000) {
                        backends[i]->_inputs.lf1 = false;
                        counter = 0;
                        test = 0;
                    }
                }
                if((backends[i]->_inputs.lt1 && backends[i]->_inputs.rf8) || test == 2) { //modx and midshield
                    test = 2;
                    const uint32_t loop = 5*frame; //should trigger sdi limits
                    if(counter % loop <= 5*hold) {
                        backends[i]->_inputs.lf1 = true;
                    } else {
                        backends[i]->_inputs.lf1 = false;
                    }
                    backends[i]->_inputs.lt1 = false;
                    backends[i]->_inputs.rf8 = false;
                    counter++;
                    if(counter > 2000) {
                        backends[i]->_inputs.lf1 = false;
                        counter = 0;
                        test = 0;
                    }
                }
                //end timing macro
                */
                backends[i]->UpdateOutputs();
                backends[i]->LimitOutputs(quarterInterval);
            }
        }
    }
}
