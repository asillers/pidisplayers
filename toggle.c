#include <gpiod.h>
#include <stdio.h>
#include <unistd.h>

int main() {
    const char *chipname = "/dev/gpiochip0";
    unsigned int line_offset = 17;   // BCM GPIO number to toggle

    // --- Open the chip ---
    struct gpiod_chip *chip = gpiod_chip_open(chipname);
    if (!chip) {
        perror("gpiod_chip_open");
        return 1;
    }

    // --- Create request config ---
    struct gpiod_request_config *req_cfg = gpiod_request_config_new();
    gpiod_request_config_set_consumer(req_cfg, "toggle_test");

    // --- Create line config ---
    struct gpiod_line_config *line_cfg = gpiod_line_config_new();

    // Set direction to output
    enum gpiod_line_value initial_value = GPIOD_LINE_VALUE_INACTIVE;
    gpiod_line_config_set_direction(line_cfg, GPIOD_LINE_DIRECTION_OUTPUT);

    // Apply this configuration to the chosen line
    gpiod_line_config_set_output_values(line_cfg, &initial_value, 1);

    // --- Request the line ---
    struct gpiod_line_request *request =
        gpiod_chip_request_lines(chip, req_cfg, line_cfg, &line_offset, 1);

    if (!request) {
        perror("gpiod_chip_request_lines");
        return 1;
    }

    // --- Toggle the GPIO pin ---
    for (int i = 0; i < 10; i++) {
        enum gpiod_line_value val =
            (i % 2 == 0) ? GPIOD_LINE_VALUE_ACTIVE : GPIOD_LINE_VALUE_INACTIVE;

        gpiod_line_request_set_values(request, &val, 1);
        printf("Set GPIO %u -> %d\n", line_offset, val);
        usleep(200000);
    }

    // --- Clean up ---
    gpiod_line_request_release(request);
    gpiod_line_config_free(line_cfg);
    gpiod_request_config_free(req_cfg);
    gpiod_chip_close(chip);

    return 0;
}