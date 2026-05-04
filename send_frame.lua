-- wyślij ramkę CAN ID 0x123 z danymi 0xAA, 0xBB, 0xCC
local data = {0xAA, 0xBB, 0xCC}
local success, err = sendFrame(0x123, data)
if not success then
    log("Błąd wysyłania: " .. err)
    end
