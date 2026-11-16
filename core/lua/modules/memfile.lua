-- A file-like view of data that is stored packed inside a save file: compressed, encrypted or wrapped.
-- It offers the same reading and writing calls as a save file, so modules like st.gvas work on it
-- unchanged. Each change packs the data again and writes it to the real file, as one undo step.
--
--   local view = memfile.view(file, unpack, pack)
--     unpack(raw) -> data, or nil and a reason
--     pack(data, raw) -> raw   (raw is the current packed content, to copy headers from)
--   local data = memfile.new("bytes")   -- a view of plain bytes with no file behind it
local memfile = {}

local sizes = { u8 = 1, s8 = 1, i8 = 1, u16 = 2, s16 = 2, i16 = 2, u32 = 4, s32 = 4, i32 = 4,
                u64 = 8, s64 = 8, i64 = 8, f32 = 4, float = 4, f64 = 8, double = 8 }
local formats = { u8 = "I1", s8 = "i1", i8 = "i1", u16 = "I2", s16 = "i2", i16 = "i2", u32 = "I4", s32 = "i4", i32 = "i4",
                  u64 = "i8", s64 = "i8", i64 = "i8", f32 = "f", float = "f", f64 = "d", double = "d" }

local View = {}
View.__index = View

local function order(self, endian)
    endian = endian or self.order
    if endian == "big" or endian == "be" then return ">" end
    return "<"
end

-- Unpacks again whenever the real file changed behind the view (an edit, undo, or a look at the original).
function View:sync()
    if not self.file then return end
    local revision = self.file:revision()
    if self.revision == revision and self.data then return end
    local data, reason = self.unpack(self.file:content())
    if not data then error(reason or "cannot unpack " .. self.file:name(), 0) end
    self.data, self.revision = data, revision
end

function View:commit()
    if not self.file then return end
    local packed = self.pack(self.data, self.file:content())
    self.file:setContent(packed)
    self.revision = self.file:revision()
end

function View:check(offset, length)
    if offset < 0 or length < 0 or offset + length > #self.data then
        error(string.format("read out of range at 0x%X in %s", offset, self:name()), 0)
    end
end

function View:name() return self.label end
function View:revision() self:sync() return self.file and self.file:revision() or self.changes end
function View:size() self:sync() return #self.data end
function View:content() self:sync() return self.data end

function View:endian(value)
    if value == nil then return self.order end
    self.order = value
    return self
end

function View:get(offset, kind, endian)
    self:sync()
    local size = sizes[kind] or error("unknown field type " .. tostring(kind))
    self:check(offset, size)
    local value = string.unpack(order(self, endian) .. formats[kind], self.data, offset + 1)
    return value
end

for kind in pairs(sizes) do
    View[kind] = function(self, offset, endian) return self:get(offset, kind, endian) end
end

function View:bytes(offset, length)
    self:sync()
    self:check(offset, length)
    return self.data:sub(offset + 1, offset + length)
end

function View:find(pattern, from)
    self:sync()
    local at = self.data:find(pattern, (from or 0) + 1, true)
    return at and at - 1 or nil
end

function View:bit(offset, bit)
    return (self:u8(offset + bit // 8) >> (bit % 8)) & 1 == 1
end

function View:str(offset, length, encoding)
    self:sync()
    if encoding == "utf16" or encoding == "utf16le" or encoding == "utf16be" then
        local big = encoding == "utf16be" or (encoding == "utf16" and self.order == "big")
        self:check(offset, length * 2)
        local chars, i = {}, 0
        while i < length do
            local unit = string.unpack(big and ">I2" or "<I2", self.data, offset + 1 + i * 2)
            if unit == 0 then break end
            if unit >= 0xD800 and unit < 0xDC00 and i + 1 < length then
                local low = string.unpack(big and ">I2" or "<I2", self.data, offset + 3 + i * 2)
                unit = 0x10000 + ((unit - 0xD800) << 10) + (low - 0xDC00)
                i = i + 1
            end
            chars[#chars + 1] = utf8.char(unit)
            i = i + 1
        end
        return table.concat(chars)
    end
    self:check(offset, length)
    local text = self.data:sub(offset + 1, offset + length)
    return (text:gsub("%z.*$", ""))
end

function View:splice(offset, length, replacement)
    self:sync()
    self:check(offset, length)
    self.data = self.data:sub(1, offset) .. replacement .. self.data:sub(offset + length + 1)
    self.changes = (self.changes or 0) + 1
    self:commit()
end

function View:setBytes(offset, replacement)
    self:splice(offset, #replacement, replacement)
end

function View:set(offset, kind, value, endian)
    local size = sizes[kind] or error("unknown field type " .. tostring(kind))
    if formats[kind] == "f" or formats[kind] == "d" then value = value + 0.0 end
    self:splice(offset, size, string.pack(order(self, endian) .. formats[kind], value))
end

function View:setBit(offset, bit, on)
    local at = offset + bit // 8
    local byte = self:u8(at)
    local mask = 1 << (bit % 8)
    self:set(at, "u8", on and (byte | mask) or (byte & ~mask & 0xFF))
end

function View:setContent(data)
    self:sync()
    self.data = data
    self.changes = (self.changes or 0) + 1
    self:commit()
end

function View:field(offset, kind, endian)
    return st.bind(function() return self:get(offset, kind, endian) end, function(value) self:set(offset, kind, value, endian) end)
end

function View:flag(offset, bit)
    return st.bind(function() return self:bit(offset, bit) end, function(on) self:setBit(offset, bit, on) end)
end

function View:text(offset, length, encoding)
    return st.bind(function() return self:str(offset, length, encoding) end, function(text)
        if encoding and encoding:match("^utf16") then
            local big = encoding == "utf16be" or (encoding == "utf16" and self.order == "big")
            local units = {}
            for _, code in utf8.codes(text) do units[#units + 1] = string.pack(big and ">I2" or "<I2", code) end
            local data = table.concat(units):sub(1, length * 2)
            self:splice(offset, length * 2, data .. string.rep("\0", length * 2 - #data))
        else
            local data = text:sub(1, length)
            self:splice(offset, length, data .. string.rep("\0", length - #data))
        end
    end)
end

local views = {}

-- One view per file name and packing, so rebuilds reuse the unpacked data.
function memfile.view(file, unpack, pack, label)
    local key = file:name() .. "\0" .. tostring(unpack) .. tostring(pack)
    local view = views[key]
    if view then
        view.file = file
        return view
    end
    view = setmetatable({ file = file, unpack = unpack, pack = pack, order = "little", label = label or file:name() }, View)
    views[key] = view
    return view
end

function memfile.new(data, label)
    return setmetatable({ data = data, order = "little", label = label or "data", changes = 0 }, View)
end

return memfile
