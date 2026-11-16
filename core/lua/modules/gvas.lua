-- Unreal Engine save games ("GVAS"), UE4 and UE5.
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
local BYTE_ARRAY_ROWS = 256

-- UE5 object versions that change the layout.
local OVERRIDABLE_SERIALIZATION = 1011
local COMPLETE_TYPE_NAME = 1012

local Reader = {}
Reader.__index = Reader

local function fail(reader, message)
    error({ gvas = true, message = message .. string.format(" at 0x%X", reader.pos) }, 0)
end

function Reader:need(count)
    if count < 0 or self.pos + count > self.size then fail(self, "unexpected end of file") end
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

-- Returns the text and where it sits, so it can be rewritten.
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

-- UE 5.4+ writes the full type as a tree: name, parameter count, then the parameters.
function Reader:typeName(depth)
    depth = depth or 0
    if depth > 16 then fail(self, "type name too deep") end
    local node = { name = self:fstring(), params = {} }
    local count = self:read("s32")
    if count < 0 or count > 64 then fail(self, "bad type name") end
    for i = 1, count do node.params[i] = self:typeName(depth + 1) end
    return node
end

local function scalar(name, kind, at, sizes)
    return { name = name, kind = kind, at = at, sizes = sizes }
end

local properties

-- `kind` is the property type, `struct` the struct name for StructProperty.
local function element(reader, name, kind, struct, sizes, bodyEnd)
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
        local fields = fixedStructs[struct]
        local width = fields and bodyEnd and (bodyEnd - reader.pos) // #fields
        if fields and (width == 4 or width == 8) then
            local node = { name = name, kind = "struct", struct = struct, children = {} }
            for _, field in ipairs(fields) do
                local at = reader.pos
                reader:skip(width)
                node.children[#node.children + 1] = scalar(field, width == 4 and "f32" or "f64", at, sizes)
            end
            return node
        elseif struct == "IntPoint" then
            local node = { name = name, kind = "struct", struct = struct, children = {} }
            for _, field in ipairs({ "x", "y" }) do
                node.children[#node.children + 1] = scalar(field, "s32", reader.pos, sizes)
                reader:skip(4)
            end
            return node
        elseif struct == "DateTime" or struct == "Timespan" then
            local at = reader.pos
            reader:skip(8)
            return scalar(name, "s64", at, sizes)
        elseif struct == "Guid" then
            local at = reader.pos
            reader:skip(16)
            return { name = name, kind = "raw", at = at, length = 16, struct = struct }
        end
        return { name = name, kind = "struct", struct = struct, children = properties(reader, sizes) }
    end
    return nil
end

local function raw(reader, name, kind, bodyStart, size)
    reader.pos = bodyStart + size
    return { name = name, kind = "raw", type = kind, at = bodyStart, length = size }
end

-- `inner` is {type =, struct =} of the elements.
local function arrayBody(reader, tag, inner, sizes, bodyStart)
    local size = tag.size
    local node = { name = tag.name, kind = "array", type = inner.type, children = {}, countAt = reader.pos }
    local count = reader:read("s32")
    local kind, struct = inner.type, inner.struct
    local elementSizes = sizes
    if kind == "StructProperty" and not reader.typeNames then
        reader:fstring()
        reader:fstring()
        local innerSize = reader.pos
        reader:skip(8)
        struct = reader:fstring()
        reader:skip(16)
        reader:guid()
        elementSizes = { table.unpack(sizes) }
        elementSizes[#elementSizes + 1] = innerSize
    end
    node.struct = struct
    if kind == "ByteProperty" and size - 4 == count and not inner.enum then
        if count > BYTE_ARRAY_ROWS then
            reader:skip(count)
            return { name = tag.name, kind = "bytes", type = "ArrayProperty", at = bodyStart + 4, length = count }
        end
    elseif kind == "ByteProperty" then
        kind = "EnumProperty"
    end
    for i = 1, count do
        local child = element(reader, "[" .. (i - 1) .. "]", kind, struct, elementSizes,
            struct and fixedStructs[struct] and reader.pos + (bodyStart + size - reader.pos) // (count - i + 1))
        if not child then return raw(reader, tag.name, "ArrayProperty", bodyStart, size) end
        node.children[i] = child
    end
    return node
end

local function mapBody(reader, tag, sizes, bodyStart)
    if tag.key.type == "StructProperty" and not tag.key.struct then return raw(reader, tag.name, "MapProperty", bodyStart, tag.size) end
    reader:read("s32")
    local count = reader:read("s32")
    local node = { name = tag.name, kind = "map", children = {} }
    for i = 1, count do
        local key = element(reader, "key", tag.key.type, tag.key.struct, sizes)
        local value = element(reader, "value", tag.value.type, tag.value.struct, sizes)
        if not key or not value then return raw(reader, tag.name, "MapProperty", bodyStart, tag.size) end
        local named = key.kind == "string" and key or key.children and key.children[1]
        local label = named and named.kind == "string" and named.value or ("[" .. (i - 1) .. "]")
        node.children[i] = { name = label, kind = "entry", key = key, value = value }
    end
    return node
end

-- {type =, struct =, enum =} from a type name tree parameter.
local function innerType(param)
    if not param then return { type = "None" } end
    local result = { type = param.name }
    if param.name == "StructProperty" and param.params[1] then result.struct = param.params[1].name end
    if param.name == "ByteProperty" and param.params[1] then
        result.enum = param.params[1].name
        result.type = "EnumProperty"
    end
    return result
end

-- UE 5.4+ tag: name, type tree, size, flags.
local function readTypedTag(reader, name)
    local tag = { name = name }
    local typeName = reader:typeName()
    tag.type = typeName.name
    tag.sizeAt = reader.pos
    tag.size = reader:read("s32")
    tag.flagsAt = reader.pos
    local flags = reader:read("u8")
    if flags & 0x01 ~= 0 then tag.index = reader:read("s32") end
    if flags & 0x02 ~= 0 then reader:skip(16) end
    if flags & 0x04 ~= 0 then
        if reader:read("u8") & 0x02 ~= 0 then reader:skip(2) end
    end
    tag.boolTrue = flags & 0x10 ~= 0
    local params = typeName.params
    if tag.type == "StructProperty" then
        tag.struct = params[1] and params[1].name
    elseif tag.type == "ByteProperty" then
        tag.enum = params[1] and params[1].name
    elseif tag.type == "ArrayProperty" or tag.type == "SetProperty" then
        tag.inner = innerType(params[1])
    elseif tag.type == "MapProperty" then
        tag.key, tag.value = innerType(params[1]), innerType(params[2])
    end
    return tag
end

local function readLegacyTag(reader, name)
    local tag = { name = name, type = reader:fstring() }
    tag.sizeAt = reader.pos
    tag.size = reader:read("s32")
    tag.index = reader:read("s32")
    local kind = tag.type
    if kind == "BoolProperty" then
        tag.boolAt = reader.pos
        reader:read("u8")
    elseif kind == "StructProperty" then
        tag.struct = reader:fstring()
        reader:skip(16)
    elseif kind == "ByteProperty" then
        local enum = reader:fstring()
        if enum ~= "None" then tag.enum = enum end
    elseif kind == "EnumProperty" then
        reader:fstring()
    elseif kind == "ArrayProperty" or kind == "SetProperty" then
        tag.inner = { type = reader:fstring() }
    elseif kind == "MapProperty" then
        tag.key, tag.value = { type = reader:fstring() }, { type = reader:fstring() }
    end
    reader:guid()
    if reader.extensions and reader:read("u8") & 0x02 ~= 0 then reader:skip(2) end
    return tag
end

local function property(reader, sizes)
    local name = reader:fstring()
    if name == "None" then return nil end
    local tag = reader.typeNames and readTypedTag(reader, name) or readLegacyTag(reader, name)
    if tag.index and tag.index > 0 then tag.name = name .. "[" .. tag.index .. "]" end
    local inner = { table.unpack(sizes) }
    inner[#inner + 1] = tag.sizeAt
    local kind = tag.type

    if kind == "BoolProperty" then
        local node
        if tag.boolAt then
            node = scalar(tag.name, "bool", tag.boolAt, inner)
        else
            node = { name = tag.name, kind = "bool", at = tag.flagsAt, mask = 0x10 }
            reader:skip(tag.size)
        end
        node.type = kind
        return node
    end

    local parse
    if kind == "StructProperty" then
        parse = function(bodyStart) return element(reader, tag.name, kind, tag.struct, inner, bodyStart + tag.size) end
    elseif kind == "ByteProperty" then
        local byte = not tag.enum and tag.size == 1
        parse = function() return element(reader, tag.name, byte and "ByteProperty" or "EnumProperty", nil, inner) end
    elseif kind == "ArrayProperty" or kind == "SetProperty" then
        parse = function(bodyStart)
            if kind == "SetProperty" then reader:read("s32") end
            return arrayBody(reader, tag, tag.inner, inner, bodyStart)
        end
    elseif kind == "MapProperty" then
        parse = function(bodyStart) return mapBody(reader, tag, inner, bodyStart) end
    else
        parse = function() return element(reader, tag.name, kind, nil, inner) end
    end

    local bodyStart = reader.pos
    reader:need(tag.size)
    local ok, node = pcall(parse, bodyStart)
    if not ok or not node or reader.pos ~= bodyStart + tag.size then node = raw(reader, tag.name, kind, bodyStart, tag.size) end
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

local function parse(file)
    local reader = setmetatable({ file = file, pos = 0, size = file:size() }, Reader)
    local ok, result = pcall(function()
        if file:size() < 4 or file:bytes(0, 4) ~= "GVAS" then fail(reader, "not an Unreal save") end
        reader.pos = 4
        local header = { save_version = reader:read("s32"), package_version = reader:read("s32") }
        if header.save_version >= 3 then header.ue5_version = reader:read("s32") end
        local ue5 = header.ue5_version or 0
        header.engine = string.format("%d.%d.%d-%d", reader:read("u16"), reader:read("u16"), reader:read("u16"), reader:read("u32"))
        header.branch = reader:fstring()
        reader:read("s32")
        local versions = reader:read("s32")
        reader:skip(versions * 20)
        header.class = reader:fstring()
        reader.extensions = ue5 >= OVERRIDABLE_SERIALIZATION and ue5 < COMPLETE_TYPE_NAME
        reader.typeNames = ue5 >= COMPLETE_TYPE_NAME
        if ue5 >= OVERRIDABLE_SERIALIZATION and reader:read("u8") & 0x02 ~= 0 then reader:skip(1) end
        return { header = header, properties = properties(reader, {}) }
    end)
    if ok then return result end
    if type(result) == "table" and result.gvas then return nil, result.message end
    error(result, 0)
end

-- Editors rebuild after every change, so the last few parses are kept by content.
local cache = {}

-- Returns {header = ..., properties = {...}} or nil and a reason.
function gvas.parse(file)
    local content = file:content()
    local key = #content .. ":" .. st.hash.crc32(content)
    for _, entry in ipairs(cache) do
        if entry.key == key and entry.content == content then return entry.result, entry.reason end
    end
    local result, reason = parse(file)
    table.insert(cache, 1, { key = key, content = content, result = result, reason = reason })
    cache[5] = nil
    return result, reason
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

-- Packed saves. Many games store the GVAS data compressed: as one zlib or gzip stream, behind a short
-- header of their own, or in Unreal's own chunked format (the FArchive compressed tag 0x9E2A83C1).
local memfile = require("st.memfile")

local PACKAGE_TAG = 0x9E2A83C1

local function isZlib(data, at)
    local cmf, flg = data:byte(at, at + 1)
    return cmf and flg and cmf & 0x0F == 8 and cmf >> 4 <= 7 and (cmf * 256 + flg) % 31 == 0
end

local function inflate(data)
    local ok, result = pcall(st.zlib.decompress, data)
    if ok then return result end
end

-- Unreal's chunked format: tag, block size, total compressed and uncompressed sizes, one size pair per
-- block, then the blocks; several such groups can follow each other.
local function unpackChunks(raw)
    local out, pos = {}, 1
    local blockSize
    while pos <= #raw do
        if #raw - pos + 1 < 32 then return nil end
        local tag, block, packedTotal, total = string.unpack("<i8i8i8i8", raw, pos)
        if tag & 0xFFFFFFFF ~= PACKAGE_TAG or tag >> 32 ~= 0 or block <= 0 then return nil end
        blockSize = blockSize or block
        pos = pos + 32
        local count = (total + block - 1) // block
        local pairs = {}
        for i = 1, count do
            if #raw - pos + 1 < 16 then return nil end
            pairs[i] = { string.unpack("<i8i8", raw, pos) }
            pos = pos + 16
        end
        for _, size in ipairs(pairs) do
            local piece = inflate(raw:sub(pos, pos + size[1] - 1))
            if not piece or #piece ~= size[2] then return nil end
            out[#out + 1] = piece
            pos = pos + size[1]
        end
        if packedTotal < 0 then return nil end
    end
    return table.concat(out), blockSize
end

local function packChunks(data, blockSize)
    local blocks, sizes, packedTotal = {}, {}, 0
    for at = 1, math.max(#data, 1), blockSize do
        local piece = data:sub(at, at + blockSize - 1)
        local packed = st.zlib.compress(piece)
        blocks[#blocks + 1] = packed
        sizes[#sizes + 1] = string.pack("<i8i8", #packed, #piece)
        packedTotal = packedTotal + #packed
    end
    return string.pack("<i8i8i8i8", PACKAGE_TAG, blockSize, packedTotal, #data) .. table.concat(sizes) .. table.concat(blocks)
end

-- In a header in front of the compressed data, 32-bit fields equal to the old sizes are taken to be
-- those sizes and are updated, so a header that records them stays right without knowing the game.
local function patchHeader(header, oldPacked, oldPlain, newPacked, newPlain)
    for at = 1, #header - 3 do
        local value = string.unpack("<I4", header, at)
        local replacement = (value == oldPacked and newPacked) or (value == oldPlain and newPlain)
        if replacement and value ~= 0 then header = header:sub(1, at - 1) .. string.pack("<I4", replacement) .. header:sub(at + 4) end
    end
    return header
end

-- How a raw file holds its GVAS data, or nil when it does not look like any known packing.
local function packing(raw)
    if raw:sub(1, 4) == "GVAS" then return { kind = "plain" } end
    if raw:sub(1, 2) == "\31\139" then
        local ok, data = pcall(st.gzip.decompress, raw)
        if ok and data:sub(1, 4) == "GVAS" then return { kind = "gzip", data = data } end
    end
    if #raw >= 32 and string.unpack("<I4", raw) == PACKAGE_TAG then
        local data, blockSize = unpackChunks(raw)
        if data and data:sub(1, 4) == "GVAS" then return { kind = "chunks", data = data, blockSize = blockSize } end
    end
    for at = 1, math.min(#raw - 1, 64) do
        if isZlib(raw, at) then
            local data = inflate(raw:sub(at))
            if data and data:sub(1, 4) == "GVAS" then return { kind = "zlib", data = data, offset = at - 1 } end
        end
    end
end

-- Opens `file` as a GVAS save: the file itself when it is plain, or a view of the unpacked data that
-- packs edits back the same way. Returns the file-like object and the packing name, or nil and a reason.
function gvas.open(file)
    if file:size() >= 4 and file:bytes(0, 4) == "GVAS" then return file, "plain" end
    local raw = file:content()
    local found = packing(raw)
    if not found then return nil, "not an Unreal save" end
    local kind, blockSize, offset = found.kind, found.blockSize, found.offset
    local view = memfile.view(file, function(current)
        local again = packing(current)
        if not again then return nil, "the save is no longer a packed Unreal save" end
        return again.data or current
    end, function(data, current)
        if kind == "gzip" then return st.gzip.compress(data) end
        if kind == "chunks" then return packChunks(data, blockSize or 131072) end
        local header = current:sub(1, offset)
        local oldPacked = #current - offset
        local oldPlain = #(inflate(current:sub(offset + 1)) or "")
        local packed = st.zlib.compress(data)
        return patchHeader(header, oldPacked, oldPlain, #packed, #data) .. packed
    end, file:name() .. " (" .. kind .. ")")
    return view, kind
end

-- A binding for a scalar or string node; strings may change length, which updates every enclosing size.
function gvas.bind(file, node)
    if node.kind == "bool" and node.mask then
        return st.bind(function() return file:u8(node.at) & node.mask ~= 0 end, function(on)
            local flags = file:u8(node.at)
            file:set(node.at, "u8", on and (flags | node.mask) or (flags & ~node.mask & 0xFF))
        end)
    elseif node.kind == "bool" then
        return st.bind(function() return file:u8(node.at) ~= 0 end, function(on) file:set(node.at, "u8", on and 1 or 0) end)
    elseif node.kind == "string" then
        return st.bind(function()
            -- Read from the file, so undo and the loaded-value view see the text as stored now.
            local reader = setmetatable({ file = file, pos = node.place.at, size = file:size() }, Reader)
            local ok, text = pcall(reader.fstring, reader)
            if ok and reader.pos - node.place.at == node.place.bytes then return text end
            return node.value
        end, function(text)
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
