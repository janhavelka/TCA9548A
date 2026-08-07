idf_component_register(
  SRCS "main.cpp"
  INCLUDE_DIRS "." "../../.."
  REQUIRES TCA9548A esp_driver_gpio esp_driver_i2c esp_rom esp_system esp_timer freertos vfs
)
