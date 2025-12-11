#include <gpiod.hpp>
#include <iostream>
#include <thread>
#include <chrono>

int main() {
    const int GPIO_RST = 6;               // your GC9 RST pin
    const char* chipname = "/dev/gpiochip0";

    try {
        gpiod::chip chip(chipname);

        // ----- line_settings -----
        gpiod::line_settings settings;
        settings.set_direction(gpiod::line::direction::OUTPUT);

        // ----- line_config -----
        gpiod::line_config lcfg;
        // use OFFSET, not {OFFSET}
        lcfg.add_line_settings(GPIO_RST, settings);

        // ----- request_config -----
        gpiod::request_config rcfg;
        rcfg.set_consumer("rst_toggle_test");

        // ----- request the line -----
        auto request = chip.request_lines(rcfg, lcfg);

        std::cout << "Toggling RST...\n";

        while (true) {
            request.set_value(GPIO_RST, gpiod::line::value::ACTIVE);
            std::this_thread::sleep_for(std::chrono::milliseconds(500));

            request.set_value(GPIO_RST, gpiod::line::value::INACTIVE);
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }
    }
    catch (const std::exception &e) {
        std::cerr << "Exception: " << e.what() << "\n";
    }
}