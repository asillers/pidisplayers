#include <gpiod.hpp>
#include <iostream>
#include <unistd.h>

int main() {
    const char* chip_name = "gpiochip0";
    unsigned int line_offset = 17;   // Example GPIO (BCM17)

    try {
        // 1. Open chip
        gpiod::chip chip(chip_name);

        // 2. Create the settings for this GPIO line
        gpiod::line_settings settings;
        settings.set_direction(gpiod::line::direction::OUTPUT);

        // 3. Create a line_config object
        gpiod::line_config lcfg;
        lcfg.add_line_settings(line_offset, settings);   // << correct v2 call

        // 4. Create a request builder bound to the chip
        gpiod::request_builder builder(chip);
        builder.set_consumer("v2-test");
        builder.set_line_config(lcfg);

        // 5. Request the line(s)
        gpiod::line_request request = builder.do_request();

        // 6. Toggle forever
        std::cout << "Toggling GPIO " << line_offset << "...\n";
        while (true) {
            request.set_value(line_offset, gpiod::line::value::ACTIVE);
            usleep(500000);

            request.set_value(line_offset, gpiod::line::value::INACTIVE);
            usleep(500000);
        }
    }
    catch (const std::exception& e) {
        std::cerr << "ERROR: " << e.what() << "\n";
        return 1;
    }
}