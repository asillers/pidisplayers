#include <algorithm>
#include <array>
#include <chrono>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <initializer_list>
#include <iostream>
#include <linux/spi/spidev.h>
#include <optional>
#include <stdexcept>
#include <string>
#include <sys/ioctl.h>
#include <thread>
#include <unistd.h>
#include <vector>

#include <gpiod.hpp>

namespace {

constexpr char SPI_PATH[] = "/dev/spidev0.0";
constexpr uint32_t SPI_SPEED_HZ = 24'000'000;   // GC9A01A is fine up to 50 MHz
constexpr uint8_t SPI_BITS = 8;
constexpr uint8_t SPI_MODE = SPI_MODE_0;

constexpr uint16_t PANEL_WIDTH = 240;
constexpr uint16_t PANEL_HEIGHT = 240;

struct ControlPins {
    unsigned int cs;
    unsigned int dc;
    unsigned int rst;
};

class Gc9Panel {
public:
    Gc9Panel(ControlPins pins, const std::string& chip = "/dev/gpiochip0")
        : pins_(pins), chip_(chip), spi_fd_(-1) {}

    ~Gc9Panel() {
        if (spi_fd_ >= 0) {
            close(spi_fd_);
        }
    }

    void init();
    void fill_color(uint16_t rgb565);

private:
    void open_spi();
    void request_lines();

    void set_pin(unsigned int offset, bool value);

    void send(uint8_t cmd, std::initializer_list<uint8_t> data = {}, uint16_t delay_ms = 0);
    void ram_write_begin(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1);
    void write_pixels(const uint8_t* data, size_t bytes);

    ControlPins pins_;
    std::string chip_;
    std::optional<gpiod::line_request> request_;
    int spi_fd_;
};

void Gc9Panel::open_spi() {
    spi_fd_ = ::open(SPI_PATH, O_RDWR);
    if (spi_fd_ < 0) {
        throw std::runtime_error("failed to open " + std::string(SPI_PATH) + ": " + std::strerror(errno));
    }

    if (ioctl(spi_fd_, SPI_IOC_WR_MODE, &SPI_MODE) < 0 ||
        ioctl(spi_fd_, SPI_IOC_WR_MAX_SPEED_HZ, &SPI_SPEED_HZ) < 0 ||
        ioctl(spi_fd_, SPI_IOC_WR_BITS_PER_WORD, &SPI_BITS) < 0) {
        throw std::runtime_error("failed to configure SPI");
    }
}

void Gc9Panel::request_lines() {
    gpiod::chip chip(chip_);
    gpiod::line_settings settings;
    settings.set_direction(gpiod::line::direction::OUTPUT);
    settings.set_output_value(gpiod::line::value::INACTIVE);

    gpiod::line_config lcfg;
    lcfg.add_line_settings(pins_.cs, settings);
    lcfg.add_line_settings(pins_.dc, settings);
    lcfg.add_line_settings(pins_.rst, settings);

    gpiod::request_config rcfg;
    rcfg.set_consumer("gc9-demo");

    gpiod::request_builder builder(chip);
    builder.set_consumer("gc9-demo");
    builder.set_line_config(lcfg);
    request_ = builder.do_request();
}

void Gc9Panel::set_pin(unsigned int offset, bool value) {
    if (!request_) {
        throw std::runtime_error("GPIO lines not requested");
    }

    request_->set_value(offset, value ? gpiod::line::value::ACTIVE : gpiod::line::value::INACTIVE);
}

void Gc9Panel::send(uint8_t cmd, std::initializer_list<uint8_t> data, uint16_t delay_ms) {
    set_pin(pins_.cs, false);
    set_pin(pins_.dc, false);
    ::write(spi_fd_, &cmd, 1);

    if (!data.size()) {
        set_pin(pins_.cs, true);
    } else {
        std::vector<uint8_t> payload(data);
        set_pin(pins_.dc, true);
        ::write(spi_fd_, payload.data(), payload.size());
        set_pin(pins_.cs, true);
    }

    if (delay_ms) {
        std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
    }
}

void Gc9Panel::ram_write_begin(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1) {
    send(0x2A, {static_cast<uint8_t>(x0 >> 8), static_cast<uint8_t>(x0 & 0xFF),
                static_cast<uint8_t>(x1 >> 8), static_cast<uint8_t>(x1 & 0xFF)});
    send(0x2B, {static_cast<uint8_t>(y0 >> 8), static_cast<uint8_t>(y0 & 0xFF),
                static_cast<uint8_t>(y1 >> 8), static_cast<uint8_t>(y1 & 0xFF)});
    send(0x2C);
    set_pin(pins_.cs, false);
    set_pin(pins_.dc, true);
}

void Gc9Panel::write_pixels(const uint8_t* data, size_t bytes) {
    ::write(spi_fd_, data, bytes);
}

void Gc9Panel::init() {
    request_lines();
    open_spi();

    // hardware reset
    set_pin(pins_.rst, false);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    set_pin(pins_.rst, true);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // GC9A01A initialisation sequence (borrowed from Adafruit GC9A01A)
    send(0xEF, {0x03, 0x80, 0x02});
    send(0xCF, {0x00, 0xC1, 0x30});
    send(0xED, {0x64, 0x03, 0x12, 0x81});
    send(0xE8, {0x85, 0x00, 0x78});
    send(0xCB, {0x39, 0x2C, 0x00, 0x34, 0x02});
    send(0xF7, {0x20});
    send(0xEA, {0x00, 0x00});

    send(0xC0, {0x23});  // power control
    send(0xC1, {0x10});
    send(0xC5, {0x3e, 0x28});
    send(0xC7, {0x86});

    send(0x36, {0x28});  // memory access
    send(0x3A, {0x55});  // 16-bit color

    send(0xB1, {0x00, 0x18});
    send(0xB6, {0x08, 0x82, 0x27});

    send(0xF2, {0x00});
    send(0x26, {0x01});

    send(0xE0, {0x0F, 0x31, 0x2B, 0x0C, 0x0E, 0x08, 0x4E, 0xF1, 0x37, 0x07,
                0x10, 0x03, 0x0E, 0x09, 0x00});  // positive gamma
    send(0xE1, {0x00, 0x0E, 0x14, 0x03, 0x11, 0x07, 0x31, 0xC1, 0x48, 0x08,
                0x0F, 0x0C, 0x31, 0x36, 0x0F});  // negative gamma

    send(0x21);        // inversion on
    send(0x11, {}, 120);
    send(0x29, {}, 20);  // display on
}

void Gc9Panel::fill_color(uint16_t rgb565) {
    ram_write_begin(0, 0, PANEL_WIDTH - 1, PANEL_HEIGHT - 1);

    std::array<uint8_t, 512> chunk{};
    for (size_t i = 0; i < chunk.size(); i += 2) {
        chunk[i] = rgb565 >> 8;
        chunk[i + 1] = rgb565 & 0xFF;
    }

    constexpr size_t total_pixels = PANEL_WIDTH * PANEL_HEIGHT;
    size_t pixels_remaining = total_pixels;

    while (pixels_remaining) {
        const size_t pixels_this_round = std::min(pixels_remaining, chunk.size() / 2);
        write_pixels(chunk.data(), pixels_this_round * 2);
        pixels_remaining -= pixels_this_round;
    }

    set_pin(pins_.cs, true);  // finish RAM write
}

}  // namespace

int main() {
    try {
        ControlPins pins{
            .cs = 7,   // GPIO7  (pin 26)
            .dc = 5,   // GPIO5  (pin 29)
            .rst = 6,  // GPIO6  (pin 31)
        };

        Gc9Panel panel(pins);
        panel.init();

        std::cout << "Filling screen RED, GREEN, BLUE...\n";

        panel.fill_color(0xF800);
        std::this_thread::sleep_for(std::chrono::seconds(2));
        panel.fill_color(0x07E0);
        std::this_thread::sleep_for(std::chrono::seconds(2));
        panel.fill_color(0x001F);
        std::this_thread::sleep_for(std::chrono::seconds(2));
        panel.fill_color(0xFFFF);

        std::cout << "Done. Display should be white.\n";
    } catch (const std::exception& ex) {
        std::cerr << "GC9 demo failed: " << ex.what() << "\n";
        return 1;
    }
    return 0;
}
