idf_component_register(SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES vfs esp_driver_gpio esp_driver_spi esp_driver_usb_serial_jtag esp_driver_uart)
