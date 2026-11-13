-- save.dat layout (little endian):
--   0x00 magic "SMPL"
--   0x04 crc32 of bytes 0x08..end
--   0x08 u32 gold
--   0x0C u8  level
--   0x0D u8  flags (bit 0: hard mode, bit 1: dlc)
--   0x10 utf16 name, 16 units

local layout = require("layout")

function detect(save)
    if not save:has("save.dat") then return false end
    return save:file("save.dat"):bytes(0, 4) == layout.magic
end

function before_save(save)
    local file = save:file("save.dat")
    file:set(layout.checksum, "u32", layout.checksumOf(file))
end
