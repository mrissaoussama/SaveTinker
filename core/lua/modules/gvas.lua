-- Unreal Engine save games ("GVAS", UE4 and UE5 before 5.4).
local gvas = {}

local fixedTypes = {
    IntProperty = "s32", Int64Property = "s64", UInt32Property = "u32", UInt64Property = "u64",
    Int16Property = "s16", UInt16Property = "u16", Int8Property = "s8", FloatProperty = "f32", DoubleProperty = "f64",
}
local typeSizes = { s8 = 1, u8 = 1, s16 = 2, u16 = 2, s32 = 4, u32 = 4, f32 = 4, s64 = 8, u64 = 8, f64 = 8 }
local stringTypes = { StrProperty = true, NameProperty = true, ObjectProperty = true, EnumProperty = true }
local fixedStructs = {
    Vector = { "x", "y", "z" }, Rotator = { "pitch", "yaw", "roll" }, Quat = { "x", "y", "z", "w" },
    Vector2D = { "x", "y" }, LinearColor = { "r", "g", "b", "a" }, Vector4 = { "x", "y", "z", "w" },
}

local Reader = {}
Reader.__index = Reader

local function fail(reader, message)
    error({ gvas = true, message = message .. string.format(" at 0x%X", reader.pos) }, 0)
end

function Reader:need(count)
    if self.pos + count > self.size then fail(self, "unexpected end of file") end
end

function Reader:read(kind)
    local size = typeSizes[kind]
    self:need(size)
    local value = self.file:get(self.pos, kind, "little")
    self.pos = self.pos + size
    return value
end

function Reader:skip(count)
    self:need(count)
    self.pos = self.pos + count
end

-- Returns the text and a description of where it sits, so it can be rewritten.
function Reader:fstring()
    local at = self.pos
    local length = self:read("s32")
    if length == 0 then return "", { at = at, bytes = 4 } end
    local text
    if length > 0 then
        self:need(length)
        text = self.file:bytes(self.pos, length - 1)
        if text:find("[\128-\255]") then
            local chars = {}
            for i = 1, #text do chars[i] = utf8.char(text:byte(i)) end
            text = table.concat(chars)
        end
        self.pos = self.pos + length
    else
        self:need(-length * 2)
        text = self.file:str(self.pos, -length - 1, "utf16le")
        self.pos = self.pos - length * 2
    end
    return text, { at = at, bytes = self.pos - at }
end

function Reader:guid()
    if self:read("u8") ~= 0 then self:skip(16) end
end

local function scalar(name, kind, at, sizes)
    return { name = name, kind = kind, at = at, sizes = sizes }
end

local properties

local function element(reader, name, kind, structName, sizes, bodyEnd)
    if fixedTypes[kind] then
        local at = reader.pos
        reader:read(fixedTypes[kind])
        return scalar(name, fixedTypes[kind], at, sizes)
    elseif kind == "BoolProperty" or kind == "ByteProperty" then
        local at = reader.pos
        reader:read("u8")
        return scalar(name, kind == "BoolProperty" and "bool" or "u8", at, sizes)
    elseif stringTypes[kind] then
        local text, place = reader:fstring()
        return { name = name, kind = "string", value = text, place = place, sizes = sizes }
    elseif kind == "StructProperty" then
        local fields = fixedStructs[structName]
        local width = fields and bodyEnd and (bodyEnd - reader.pos) // #fields
        if fields and (width == 4 or width == 8) then
            local node = { name = name, kind = "struct", struct = structName, children = {} }
            for _, field in ipairs(fields) do
                local at = reader.pos
                reader:skip(width)
                node.children[#node.children + 1] = scalar(field, width == 4 and "f32" or "f64", at, sizes)
            end
            return node
        elseif structName == "IntPoint" then
            local node = { name = name, kind = "struct", struct = structName, children = {} }
            for _, field in ipairs({ "x", "y" }) do
                node.children[#node.children + 1] = scalar(field, "s32", reader.pos, sizes)
                reader:skip(4)
            end
            return node
        elseif structName == "DateTime" or structName == "Timespan" then
            local at = reader.pos
            reader:skip(8)
            return scalar(name, "s64", at, sizes)
        elseif structName == "Guid" then
            local at = reader.pos
            reader:skip(16)
            return { name = name, kind = "raw", at = at, length = 16, struct = structName }
        end
        return { name = name, kind = "struct", struct = structName, children = properties(reader, sizes) }
    end
    return nil
end

local function raw(reader, name, kind, bodyStart, size)
    reader.pos = bodyStart + size
    return { name = name, kind = "raw", type = kind, at = bodyStart, length = size }
end

local function arrayBody(reader, name, inner, sizes, bodyStart, size)
    local node = { name = name, kind = "array", type = inner, children = {}, countAt = reader.pos }
    local count = reader:read("s32")
    local structName
    local elementSizes = sizes
    if inner == "StructProperty" then
        reader:fstring()
        reader:fstring()
        local innerSize = reader.pos
        reader:skip(8)
        structName = reader:fstring()
        reader:skip(16)
        reader:guid()
        elementSizes = { table.unpack(sizes) }
        elementSizes[#elementSizes + 1] = innerSize
        node.struct = structName
    end
    if inner == "ByteProperty" and size - 4 ~= count then inner = "EnumProperty" end
    for i = 1, count do
        local child = element(reader, "[" .. (i - 1) .. "]", inner, structName, elementSizes,
            structName and fixedStructs[structName] and reader.pos + (bodyStart + size - reader.pos) // count)
        if not child then return raw(reader, name, "ArrayProperty", bodyStart, size) end
        node.children[i] = child
    end
    return node
end

local function mapBody(reader, name, keyType, valueType, sizes, bodyStart, size)
    if keyType == "StructProperty" then return raw(reader, name, "MapProperty", bodyStart, size) end
    reader:read("s32")
    local count = reader:read("s32")
    local node = { name = name, kind = "map", children = {} }
    for i = 1, count do
        local key = element(reader, "key", keyType, nil, sizes)
        local value = element(reader, "value", valueType, nil, sizes)
        if not key or not value then return raw(reader, name, "MapProperty", bodyStart, size) end
        local label = key.kind == "string" and key.value or ("[" .. (i - 1) .. "]")
        node.children[i] = { name = label, kind = "entry", key = key, value = value }
    end
    return node
end

local function property(reader, sizes)
    local name = reader:fstring()
    if name == "None" then return nil end
    local kind = reader:fstring()
    local sizeAt = reader.pos
    local size = reader:read("s32")
    local index = reader:read("s32")
    if index > 0 then name = name .. "[" .. index .. "]" end
    local inner = { table.unpack(sizes) }
    inner[#inner + 1] = sizeAt

    if kind == "BoolProperty" then
        local at = reader.pos
        reader:read("u8")
        reader:guid()
        local node = scalar(name, "bool", at, inner)
        node.type = kind
        return node
    end

    local parse
    if kind == "StructProperty" then
        local structName = reader:fstring()
        reader:skip(16)
        reader:guid()
        parse = function(bodyStart) return element(reader, name, kind, structName, inner, bodyStart + size) end
    elseif kind == "ByteProperty" then
        local enumName = reader:fstring()
        reader:guid()
        local byte = enumName == "None" and size == 1
        parse = function() return element(reader, name, byte and "ByteProperty" or "EnumProperty", nil, inner) end
    elseif kind == "EnumProperty" then
        reader:fstring()
        reader:guid()
        parse = function() return element(reader, name, kind, nil, inner) end
    elseif kind == "ArrayProperty" or kind == "SetProperty" then
        local innerType = reader:fstring()
        reader:guid()
        parse = function(bodyStart)
            if kind == "SetProperty" then reader:read("s32") end
            return arrayBody(reader, name, innerType, inner, bodyStart, size)
        end
    elseif kind == "MapProperty" then
        local keyType = reader:fstring()
        local valueType = reader:fstring()
        reader:guid()
        parse = function(bodyStart) return mapBody(reader, name, keyType, valueType, inner, bodyStart, size) end
    else
        reader:guid()
        parse = function() return element(reader, name, kind, nil, inner) end
    end

    local bodyStart = reader.pos
    local ok, node = pcall(parse, bodyStart)
    if not ok or not node or reader.pos ~= bodyStart + size then node = raw(reader, name, kind, bodyStart, size) end
    node.type = node.type or kind
    return node
end

function properties(reader, sizes)
    local list = {}
    while true do
        local node = property(reader, sizes)
        if not node then return list end
        list[#list + 1] = node
    end
end

-- Returns {header = ..., properties = {...}} or nil and a reason.
function gvas.parse(file)
    local reader = setmetatable({ file = file, pos = 0, size = file:size() }, Reader)
    local ok, result = pcall(function()
        if file:size() < 4 or file:bytes(0, 4) ~= "GVAS" then fail(reader, "not an Unreal save") end
        reader.pos = 4
        local header = { save_version = reader:read("s32"), package_version = reader:read("s32") }
        if header.save_version >= 3 then header.ue5_version = reader:read("s32") end
        if header.ue5_version and header.ue5_version >= 1012 then
            fail(reader, "Unreal 5.4 and later saves are not supported yet")
        end
        header.engine = string.format("%d.%d.%d-%d", reader:read("u16"), reader:read("u16"), reader:read("u16"), reader:read("u32"))
        header.branch = reader:fstring()
        reader:read("s32")
        local versions = reader:read("s32")
        reader:skip(versions * 20)
        header.class = reader:fstring()
        return { header = header, properties = properties(reader, {}) }
    end)
    if ok then return result end
    if type(result) == "table" and result.gvas then return nil, result.message end
    error(result, 0)
end

local function encodeString(text)
    if text == "" then return string.pack("<i4", 0) end
    if not text:find("[\128-\255]") then return string.pack("<i4", #text + 1) .. text .. "\0" end
    local units = {}
    for _, code in utf8.codes(text) do
        if code >= 0x10000 then
            code = code - 0x10000
            units[#units + 1] = string.pack("<I2I2", 0xD800 + (code >> 10), 0xDC00 + (code & 0x3FF))
        else
            units[#units + 1] = string.pack("<I2", code)
        end
    end
    units[#units + 1] = "\0\0"
    local body = table.concat(units)
    return string.pack("<i4", -(#body // 2)) .. body
end

-- A binding for a scalar or string node; strings may change length, which updates every enclosing size.
function gvas.bind(file, node)
    if node.kind == "bool" then
        return st.bind(function() return file:u8(node.at) ~= 0 end, function(on) file:set(node.at, "u8", on and 1 or 0) end)
    elseif node.kind == "string" then
        return st.bind(function() return node.value end, function(text)
            local encoded = encodeString(text)
            local delta = #encoded - node.place.bytes
            st.transaction(node.name, function()
                file:splice(node.place.at, node.place.bytes, encoded)
                for _, at in ipairs(node.sizes) do file:set(at, "s32", file:s32(at) + delta) end
            end)
        end)
    elseif typeSizes[node.kind] then
        return file:field(node.at, node.kind, "little")
    end
end

return gvas
