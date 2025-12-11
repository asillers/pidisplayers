#include <array>
#include <chrono>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <linux/spi/spidev.h>
#include <stdexcept>
#include <string>
#include <sys/ioctl.h>
#include <thread>
#include <unistd.h>
#include <vector>

#include <gpiod.hpp>

namespace {

constexpr char SPI_PATH[] = "/dev/spidev0.0";  // CE0 wired to the EPD CS
constexpr uint32_t SPI_SPEED_HZ = 4'000'000;
constexpr uint8_t SPI_BITS = 8;
constexpr uint8_t SPI_MODE = SPI_MODE_0;

constexpr int EPD_WIDTH = 128;
constexpr int EPD_HEIGHT = 296;
constexpr size_t BUFFER_SIZE = (EPD_WIDTH * EPD_HEIGHT) / 8;

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
    void show_demo();
    void deep_sleep();

private:
    void open_spi();
    void request_lines();
    void reset();
    void wait_busy(const std::string& stage);
    void send_cmd(uint8_t cmd);
    void send_data(const uint8_t* data, size_t len);
    void send_data(uint8_t byte);

    Pins pins_;
    std::string chip_;
    gpiod::line line_dc_;
    gpiod::line line_rst_;
    gpiod::line line_busy_;
    int spi_fd_;
};

void Epd29::open_spi() {
    spi_fd_ = ::open(SPI_PATH, O_RDWR);
    if (spi_fd_ < 0) {
        throw std::runtime_error("failed to open " + std::string(SPI_PATH) + ": " + std::strerror(errno));
    }

    if (ioctl(spi_fd_, SPI_IOC_WR_MODE, &SPI_MODE) < 0 ||
        ioctl(spi_fd_, SPI_IOC_WR_MAX_SPEED_HZ, &SPI_SPEED_HZ) < 0 ||
        ioctl(spi_fd_, SPI_IOC_WR_BITS_PER_WORD, &SPI_BITS) < 0) {
        throw std::runtime_error("failed to configure SPI for EPD");
    }
}

void Epd29::request_lines() {
    gpiod::chip chip(chip_);

    gpiod::line_request_config out_cfg;
    out_cfg.consumer = "epd-demo";
    out_cfg.request_type = gpiod::line_request::DIRECTION_OUTPUT;

    gpiod::line_request_config in_cfg;
    in_cfg.consumer = "epd-demo";
    in_cfg.request_type = gpiod::line_request::DIRECTION_INPUT;
    in_cfg.flags = gpiod::line_request::FLAG_BIAS_PULL_UP;

    line_dc_ = chip.get_line(pins_.dc);
    line_rst_ = chip.get_line(pins_.rst);
    line_busy_ = chip.get_line(pins_.busy);

    line_dc_.request(out_cfg, 0);
    line_rst_.request(out_cfg, 1);
    line_busy_.request(in_cfg, 0);
}

void Epd29::reset() {
    line_rst_.set_value(1);
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    line_rst_.set_value(0);
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    line_rst_.set_value(1);
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
}

void Epd29::wait_busy(const std::string& stage) {
    while (line_busy_.get_value() == 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    if (!stage.empty()) {
        std::cout << stage << " complete\n";
    }
}

void Epd29::send_cmd(uint8_t cmd) {
    line_dc_.set_value(0);
    ::write(spi_fd_, &cmd, 1);
}

void Epd29::send_data(uint8_t byte) {
    line_dc_.set_value(1);
    ::write(spi_fd_, &byte, 1);
}

void Epd29::send_data(const uint8_t* data, size_t len) {
    line_dc_.set_value(1);
    ::write(spi_fd_, data, len);
}

void Epd29::init() {
    request_lines();
    open_spi();
    reset();

    send_cmd(0x06);  // booster soft start
    send_data(0x17);
    send_data(0x17);
    send_data(0x17);

    send_cmd(0x04);  // power on
    wait_busy("power on");

    send_cmd(0x00);  // panel setting
    send_data(0x0F); // KW-BF + BWROTP

    send_cmd(0x50);  // VCOM + data interval
    send_data(0xF7);

    send_cmd(0x30);  // PLL control
    send_data(0x3C);

    send_cmd(0x61);  // resolution
    send_data(EPD_WIDTH >> 8);
    send_data(EPD_WIDTH & 0xFF);
    send_data(EPD_HEIGHT >> 8);
    send_data(EPD_HEIGHT & 0xFF);

    send_cmd(0x82);  // VCOM voltage
    send_data(0x12);
}

void Epd29::show_demo() {
    std::array<uint8_t, BUFFER_SIZE> black{};
    std::array<uint8_t, BUFFER_SIZE> red{};

    // Checkerboard pattern
    for (size_t i = 0; i < BUFFER_SIZE; ++i) {
        black[i] = (i & 0x1) ? 0xFF : 0x00;  // alternating stripes
        red[i] = 0xFF;  // white for b/w panel
    }

    send_cmd(0x10);  // data start transmission 1 (old data)
    send_data(black.data(), black.size());

    send_cmd(0x13);  // data start transmission 2 (new data)
    send_data(red.data(), red.size());

    send_cmd(0x12);  // display refresh
    wait_busy("refresh");
}

void Epd29::deep_sleep() {
    send_cmd(0x02);  // power off
    wait_busy("power off");
    send_cmd(0x07);
    send_data(0xA5);  // deep sleep
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
        epd.show_demo();
        epd.deep_sleep();

        std::cout << "EPD demo complete\n";
    } catch (const std::exception& ex) {
        std::cerr << "EPD demo failed: " << ex.what() << "\n";
        return 1;
    }

    return 0;
}
