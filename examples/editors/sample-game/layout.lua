-- save.dat, little endian, 0x50 bytes
local layout = {
    magic = "SMPL",
    checksum = 0x04, -- crc32 of 0x08..end
    gold = 0x08,     -- u32
    level = 0x0C,    -- u8
    flags = 0x0D,    -- bit 0 hard mode, bit 1 dlc
    class = 0x0E,    -- u8
    quest = 0x0F,    -- u8: 0 locked, 1 active, 2 done
    name = 0x10,     -- utf16, 16 units
    dlcLevel = 0x30,
    dlcArea = 0x31,
    dlcBoss = 0x32,
    elements = 0x33, -- bit flags
    bag = 0x38,      -- 8 x u16 item ids
    bagSlots = 8,
    mana = 0x48,
    size = 0x50,
}

function layout.checksumOf(file)
    return st.hash.crc32(file:bytes(0x08, file:size() - 0x08))
end

return layout
