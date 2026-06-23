#ifndef I2C_BUS_H
#define I2C_BUS_H

#include "driver/i2c_master.h"
#include "esp_err.h"

/* Initialize the shared I2C master bus using board_config.h. */
esp_err_t i2c_bus_init(void);

/* Probe the usable 7-bit address range and log every responding address. */
esp_err_t i2c_bus_scan(void);

/* Return the shared bus handle, or NULL when the bus is not initialized. */
i2c_master_bus_handle_t i2c_bus_get_handle(void);

/* Release the I2C master bus after all connected drivers are deinitialized. */
esp_err_t i2c_bus_deinit(void);

#endif /* I2C_BUS_H */
