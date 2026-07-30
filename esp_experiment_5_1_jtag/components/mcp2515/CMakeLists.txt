# Zwendorowana (skopiowana 1:1) biblioteka Arduino "autowp-mcp2515" z lokalnej
# instalacji Arduino IDE tego uzytkownika (~/Arduino/libraries/autowp-mcp2515),
# ta sama wersja co uzywana przez esp_experiment_5_1.ino - celowo NIE
# przepisana od zera, zeby nie ryzykowac bledow w adresach rejestrow/timingu
# SPI kontrolera MCP2515. Wymaga Arduino.h/SPI.h z komponentu arduino-esp32.
idf_component_register(
    SRCS "mcp2515.cpp"
    INCLUDE_DIRS "."
    REQUIRES arduino-esp32
)
