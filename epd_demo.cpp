#include <array>
#include <chrono>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <fcntl.h>
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

constexpr char SPI_PATH[] = "/dev/spidev0.0";  // CE0 -> panel CS
constexpr uint32_t SPI_SPEED_HZ = 4'000'000;
constexpr uint8_t SPI_BITS = 8;
constexpr uint8_t SPI_MODE = SPI_MODE_0;

constexpr int PANEL_WIDTH = 128;
constexpr int PANEL_HEIGHT = 296;
constexpr size_t BUFFER_SIZE = (PANEL_WIDTH * PANEL_HEIGHT) / 8;
constexpr bool BUSY_ACTIVE_HIGH = true;  // Waveshare 2.9" V2 keeps BUSY high while busy

struct Pins {
    unsigned int dc;
    unsigned int rst;
    unsigned int busy;
};

class Epd29 {
public:
    explicit Epd29(Pins pins, const std::string& chip = "/dev/gpiochip0")
        : pins_(pins), chip_(chip), spi_fd_(-1) {}

    ~Epd29() {
        if (spi_fd_ >= 0) {
            close(spi_fd_);
        }
    }

    void init();
    void clear();
    void demo_pattern();
    void deep_sleep();

private:
    void request_lines();
    void open_spi();
    void reset();

    bool busy_asserted() const;
    void wait_busy(const std::string& stage, std::chrono::milliseconds poll{20});

    void send_cmd(uint8_t cmd);
    void send_data(uint8_t byte);
    void send_data(const uint8_t* data, size_t len);

    Pins pins_;
    std::string chip_;
    std::optional<gpiod::line_request> request_;
    int spi_fd_;
};

void Epd29::request_lines() {
    gpiod::chip chip(chip_);

    gpiod::line_settings dc_settings;
    dc_settings.set_direction(gpiod::line::direction::OUTPUT);
    dc_settings.set_output_value(gpiod::line::value::INACTIVE);

    gpiod::line_settings rst_settings = dc_settings;
    rst_settings.set_output_value(gpiod::line::value::ACTIVE);

    gpiod::line_settings busy_settings;
    busy_settings.set_direction(gpiod::line::direction::INPUT);
    busy_settings.set_bias(gpiod::line::bias::PULL_UP);

    gpiod::line_config lcfg;
    lcfg.add_line_settings(pins_.dc, dc_settings);
    lcfg.add_line_settings(pins_.rst, rst_settings);
    lcfg.add_line_settings(pins_.busy, busy_settings);

    auto builder = chip.prepare_request();
    builder.set_consumer("epd-demo");
    builder.set_line_config(lcfg);
    request_ = builder.do_request();
}

void Epd29::open_spi() {
    spi_fd_ = ::open(SPI_PATH, O_RDWR);
    if (spi_fd_ < 0) {
        throw std::runtime_error("failed to open SPI: " + std::string(std::strerror(errno)));
    }

    if (ioctl(spi_fd_, SPI_IOC_WR_MODE, &SPI_MODE) < 0 ||
        ioctl(spi_fd_, SPI_IOC_WR_MAX_SPEED_HZ, &SPI_SPEED_HZ) < 0 ||
        ioctl(spi_fd_, SPI_IOC_WR_BITS_PER_WORD, &SPI_BITS) < 0) {
        throw std::runtime_error("failed to configure SPI");
    }
}

void Epd29::reset() {
    if (!request_) {
        throw std::runtime_error("lines not requested");
    }

    request_->set_value(pins_.rst, gpiod::line::value::ACTIVE);
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    request_->set_value(pins_.rst, gpiod::line::value::INACTIVE);
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    request_->set_value(pins_.rst, gpiod::line::value::ACTIVE);
    std::this_thread::sleep_for(std::chrono::milliseconds(120));
}

bool Epd29::busy_asserted() const {
    if (!request_) {
        throw std::runtime_error("lines not requested");
    }

    auto value = request_->get_value(pins_.busy);
    return BUSY_ACTIVE_HIGH ? value == gpiod::line::value::ACTIVE
                            : value == gpiod::line::value::INACTIVE;
}

void Epd29::wait_busy(const std::string& stage, std::chrono::milliseconds poll) {
    auto start = std::chrono::steady_clock::now();
    auto timeout = start + std::chrono::seconds(10);

    while (busy_asserted()) {
        if (std::chrono::steady_clock::now() > timeout) {
            throw std::runtime_error(stage + " timeout waiting for BUSY release");
        }
        std::this_thread::sleep_for(poll);
    }

    if (!stage.empty()) {
        std::cout << stage << " complete\n";
    }
}

void Epd29::send_cmd(uint8_t cmd) {
    request_->set_value(pins_.dc, gpiod::line::value::INACTIVE);
    ::write(spi_fd_, &cmd, 1);
}

void Epd29::send_data(uint8_t byte) {
    request_->set_value(pins_.dc, gpiod::line::value::ACTIVE);
    ::write(spi_fd_, &byte, 1);
}

void Epd29::send_data(const uint8_t* data, size_t len) {
    request_->set_value(pins_.dc, gpiod::line::value::ACTIVE);
    ::write(spi_fd_, data, len);
}

void Epd29::init() {
    request_lines();
    open_spi();

    reset();

    // Booster soft start
    send_cmd(0x06);
    send_data(0x17);
    send_data(0x17);
    send_data(0x17);

    // Power on
    send_cmd(0x04);
    wait_busy("power on");

    // Panel settings (KW-BF, BWROTP)
    send_cmd(0x00);
    send_data(0x0F);

    // VCOM / data interval
    send_cmd(0x50);
    send_data(0xF7);

    // PLL control
    send_cmd(0x30);
    send_data(0x3C);

    // Resolution (X, Y)
    send_cmd(0x61);
    send_data(PANEL_WIDTH >> 8);
    send_data(PANEL_WIDTH & 0xFF);
    send_data(PANEL_HEIGHT >> 8);
    send_data(PANEL_HEIGHT & 0xFF);

    // VCOM voltage
    send_cmd(0x82);
    send_data(0x12);
}

void Epd29::clear() {
    std::array<uint8_t, BUFFER_SIZE> white;
    white.fill(0xFF);

    send_cmd(0x10);  // Old data
    send_data(white.data(), white.size());
    send_cmd(0x13);  // New data
    send_data(white.data(), white.size());

    send_cmd(0x12);  // Refresh
    wait_busy("clear refresh");
}

void Epd29::demo_pattern() {
    std::array<uint8_t, BUFFER_SIZE> old_frame;
    std::array<uint8_t, BUFFER_SIZE> new_frame;

    old_frame.fill(0xFF);
    new_frame.fill(0xFF);

    // Horizontal stripes: alternate full-black and full-white rows.
    for (int row = 0; row < PANEL_HEIGHT; ++row) {
        bool black_row = (row / 16) % 2 == 0;
        for (int col = 0; col < PANEL_WIDTH; ++col) {
            size_t bit_index = row * PANEL_WIDTH + col;
            size_t byte_index = bit_index / 8;
            uint8_t bit_mask = 0x80 >> (bit_index % 8);

            if (black_row) {
                new_frame[byte_index] &= ~bit_mask;  // drive black (0)
            } else {
                new_frame[byte_index] |= bit_mask;   // leave white (1)
            }
        }
    }

    send_cmd(0x10);
    send_data(old_frame.data(), old_frame.size());

    send_cmd(0x13);
    send_data(new_frame.data(), new_frame.size());

    send_cmd(0x12);
    wait_busy("refresh");
}

void Epd29::deep_sleep() {
    send_cmd(0x02);  // power off
    wait_busy("power off");
    send_cmd(0x07);  // deep sleep
    send_data(0xA5);
}

}  // namespace

int main() {
    try {
        Pins pins{
            .dc = 25,   // GPIO25 (pin 22)
            .rst = 17,  // GPIO17 (pin 11)
            .busy = 24  // GPIO24 (pin 18)
        };

        Epd29 epd(pins);
        epd.init();
        epd.clear();
        epd.demo_pattern();
        epd.deep_sleep();

        std::cout << "EPD demo complete\n";
    } catch (const std::exception& ex) {
        std::cerr << "EPD demo failed: " << ex.what() << "\n";
        return 1;
    }

    return 0;
}
