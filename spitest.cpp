#include <gpiod.hpp>
#include <iostream>
#include <chrono>
#include <thread>

int main() {
    const int GPIO_RST = 6;      // change to any GPIO

    try {
        // open GPIO chip
        gpiod::chip chip("gpiochip0");

        // configure settings for this line
        gpiod::line_settings settings;
        settings.set_direction(gpiod::line::direction::OUTPUT);

        // attach these settings to the line
        gpiod::line_config lcfg;
        lcfg.add_line_settings({GPIO_RST}, settings);

        // request this line
        gpiod::request req = chip.request_lines(
            gpiod::request_config().set_consumer("rst_test"),
            lcfg
        );

        std::cout << "Toggling RST...\n";

        for (;;) {
            req.set_value(GPIO_RST, gpiod::line::value::ACTIVE);
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            req.set_value(GPIO_RST, gpiod::line::value::INACTIVE);
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }

    } catch (const std::exception& e) {
        std::cerr << "Exception: " << e.what() << "\n";
    }

    return 0;
}