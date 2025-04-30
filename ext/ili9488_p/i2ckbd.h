#include <pico/stdlib.h>
#include <pico/platform.h>
#include <hardware/gpio.h>
#include <hardware/i2c.h>

#define I2C_KBD_MOD i2c1
#define I2C_KBD_SDA 6
#define I2C_KBD_SCL 7
#define I2C_KBD_SPEED 400000
#define I2C_KBD_ADDR 0x1F

void init_i2c_kbd();
int write_i2c_kbd();
int read_i2c_kbd();
int check_if_failed();
void reset_failed();
int I2C_Send_RegData(int i2caddr, int reg, char command);