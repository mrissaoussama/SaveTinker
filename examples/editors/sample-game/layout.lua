local layout = {
    magic = "SMPL",
    checksum = 0x04,
    gold = 0x08,
    level = 0x0C,
    flags = 0x0D,
    name = 0x10,
    nameLength = 16,
}

function layout.checksumOf(file)
    return st.hash.crc32(file:bytes(0x08, file:size() - 0x08))
end

return layout
