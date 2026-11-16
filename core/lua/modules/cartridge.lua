-- Helpers for GBA and DS save images, which emulators and flash carts write in slightly different ways.
--   local cart = require("st.cartridge")
--   cart.size(file)       size of the save data itself, without an emulator footer
--   cart.footer(file)     "desmume" when a DeSmuME footer follows the data, else nil
--   cart.fits(file, ...)  true when the data size is one of the given sizes
-- Offsets inside the data are the same with or without a footer, which always sits at the end.
local cartridge = {}

local DESMUME_TAIL = "|-DESMUME SAVE-|"
local DESMUME_FOOTER = 122

function cartridge.footer(file)
    local size = file:size()
    if size >= DESMUME_FOOTER and file:bytes(size - #DESMUME_TAIL, #DESMUME_TAIL) == DESMUME_TAIL then return "desmume" end
end

function cartridge.size(file)
    local size = file:size()
    if cartridge.footer(file) == "desmume" then
        -- The footer records the data size; fall back to the footer length when it does not fit.
        local recorded = file:u32(size - DESMUME_FOOTER + 82)
        if recorded > 0 and recorded <= size - DESMUME_FOOTER then return recorded end
        return size - DESMUME_FOOTER
    end
    return size
end

function cartridge.fits(file, ...)
    local size = cartridge.size(file)
    for _, wanted in ipairs({ ... }) do
        if size == wanted then return true end
    end
    -- Some tools pad saves to the next power of two.
    for _, wanted in ipairs({ ... }) do
        if size > wanted and size % wanted == 0 and size <= wanted * 2 then return true end
    end
    return false
end

return cartridge
