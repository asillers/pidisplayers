#include <gpiod.h>
#include <stdio.h>
#include <unistd.h>

int main(void) {
    const char *chipname = "/dev/gpiochip0";
    unsigned int line_offset = 17;

    /* === Open chip === */
    struct gpiod_chip *chip = gpiod_chip_open(chipname);
    if (!chip) {
        perror("gpiod_chip_open");
        return 1;
    }

    /* === Configure the line as OUTPUT === */
    struct gpiod_line_config *lcfg = gpiod_line_config_new();
    if (!lcfg) {
        perror("gpiod_line_config_new");
        return 1;
    }

    enum gpiod_line_value init_val = GPIOD_LINE_VALUE_INACTIVE;

    if (gpiod_line_config_add_line_settings(
            lcfg,
            &line_offset, 1,
            gpiod_line_settings_new_output(init_val)
        ) < 0) {
        perror("gpiod_line_config_add_line_settings");
        return 1;
    }

    /* === Build request configuration === */
    struct gpiod_request_config *rcfg = gpiod_request_config_new();
    if (!rcfg) {
        perror("gpiod_request_config_new");
        return 1;
    }
    gpiod_request_config_set_consumer(rcfg, "toggle_v2_test");

    /* === Request the line === */
    struct gpiod_line_request *request =
        gpiod_chip_request_lines(chip, rcfg, lcfg);

    if (!request) {
        perror("gpiod_chip_request_lines");
        return 1;
    }

    printf("Toggling GPIO %u\n", line_offset);

    /* === Toggle the output === */
    for (int i = 0; i < 10; i++) {
        gpiod_line_request_set_value(request, line_offset, GPIOD_LINE_VALUE_ACTIVE);
        usleep(500000);

        gpiod_line_request_set_value(request, line_offset, GPIOD_LINE_VALUE_INACTIVE);
        usleep(500000);
    }

    /* === Cleanup === */
    gpiod_line_request_release(request);
    gpiod_chip_close(chip);
    return 0;
}