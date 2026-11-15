#include "accelerometer.h"
#include "sdkconfig.h"
#if CONFIG_BRIDGE_QMI8658
#include "driver/i2c_master.h"
#include "driver/gpio.h"

static i2c_master_bus_handle_t bus;
static i2c_master_dev_handle_t device;
static esp_err_t read_reg(uint8_t reg, uint8_t *data, size_t length)
{
    return i2c_master_transmit_receive(device, &reg, 1, data, length, 50);
}
static esp_err_t write_reg(uint8_t reg, uint8_t value)
{
    uint8_t data[] = {reg, value};
    return i2c_master_transmit(device, data, sizeof(data), 50);
}
bool accelerometer_init(void)
{
    if (CONFIG_BRIDGE_IMU_SDA == CONFIG_BRIDGE_IMU_SCL) return false;
    i2c_master_bus_config_t config = {
        .i2c_port = -1, .sda_io_num = CONFIG_BRIDGE_IMU_SDA,
        .scl_io_num = CONFIG_BRIDGE_IMU_SCL, .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7, .flags.enable_internal_pullup = true,
    };
    if (i2c_new_master_bus(&config, &bus) != ESP_OK) return false;
    i2c_device_config_t dev = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7, .device_address = CONFIG_BRIDGE_IMU_ADDRESS,
        .scl_speed_hz = 100000,
    };
    if (i2c_master_bus_add_device(bus, &dev, &device) != ESP_OK) goto fail;
    uint8_t id = 0;
    // QMI8658C datasheet: WHO_AM_I=0x05, auto-increment + little-endian,
    // accelerometer only, +/-2g, 62.5Hz. Gyroscope remains disabled.
    if (read_reg(0x00, &id, 1) != ESP_OK || id != 0x05 ||
        write_reg(0x08, 0x00) != ESP_OK || write_reg(0x02, 0x40) != ESP_OK ||
        write_reg(0x03, 0x07) != ESP_OK || write_reg(0x08, 0x01) != ESP_OK) goto fail;
    return true;
fail:
    if (device) { i2c_master_bus_rm_device(device); device = NULL; }
    i2c_del_master_bus(bus);
    bus = NULL;
    // Release optional pins after an unsuccessful probe.
    gpio_reset_pin(CONFIG_BRIDGE_IMU_SDA);
    gpio_reset_pin(CONFIG_BRIDGE_IMU_SCL);
    return false;
}
acceleration_result_t accelerometer_read(acceleration_t *sample)
{
    uint8_t ready, data[6];
    if (!device || read_reg(0x2e, &ready, 1) != ESP_OK) return ACCELEROMETER_ERROR;
    if (!(ready & 1)) return ACCELEROMETER_WAIT;
    if (read_reg(0x35, data, sizeof(data)) != ESP_OK) return ACCELEROMETER_ERROR;
    int axes[3];
    for (int i = 0; i < 3; i++)
        // Accelerometers measure support acceleration, opposite to gravity.
        axes[i] = -(int16_t)(data[2*i] | (data[2*i+1] << 8)) * 1000 / 16384;
#if CONFIG_BRIDGE_IMU_SWAP_XY
    int saved = axes[0]; axes[0] = axes[1]; axes[1] = saved;
#endif
#if CONFIG_BRIDGE_IMU_INVERT_X
    axes[0] = -axes[0];
#endif
#if CONFIG_BRIDGE_IMU_INVERT_Y
    axes[1] = -axes[1];
#endif
    *sample = (acceleration_t){axes[0], axes[1], axes[2]};
    return ACCELEROMETER_SAMPLE;
}
#else
bool accelerometer_init(void) { return false; }
acceleration_result_t accelerometer_read(acceleration_t *sample) { (void)sample; return ACCELEROMETER_ERROR; }
#endif
