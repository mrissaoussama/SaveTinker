function st.bind(get, set)
    return {
        get = function() return get() end,
        set = function(_, value) set(value) end,
    }
end

function st.game.has_dlc(id)
    id = string.upper(id)
    for _, owned in ipairs(st.game.dlc) do
        if owned == id or owned:sub(-#id) == id then return true end
    end
    return false
end

-- Compares the running game version with a pattern like ">=1.1.0" or "1.0.0 - 1.2.0".
function st.game.version_is(pattern)
    return st.version_matches(pattern, st.game.version)
end

function st.labels(path)
    local dir, file = path:match("^(.*)/([^/]+)$")
    local text = dir and st.data(dir .. "/" .. st.lang .. "/" .. file, true)
    local raw = st.json.decode(text or st.data(path))
    local labels = {}
    for key, value in pairs(raw) do
        labels[math.tointeger(tonumber(key)) or key] = value
    end
    return labels
end

local typeSizes = {
    u8 = 1, s8 = 1, i8 = 1, u16 = 2, s16 = 2, i16 = 2, u32 = 4, s32 = 4, i32 = 4, f32 = 4, float = 4,
    u64 = 8, s64 = 8, i64 = 8, f64 = 8, double = 8,
}
local textUnits = { utf8 = 1, utf16 = 2, utf16le = 2, utf16be = 2 }

local Struct = {}
Struct.__index = Struct

local function fieldSize(field)
    local kind = field.type
    if getmetatable(kind) == Struct then return kind.size end
    if typeSizes[kind] then return typeSizes[kind] end
    if textUnits[kind] then return field.length * textUnits[kind] end
    if kind == "bytes" or kind == "pad" then return field.length end
    if kind == "bits" then return math.ceil(field.length / 8) end
    error("unknown field type '" .. tostring(kind) .. "'", 3)
end

-- st.struct{ {"level", "u8"}, {"name", "utf16", 8}, {"_", "pad", 2}, {"stats", "u16", count = 4},
--            {"pos", Vector}, {"flags", "bits", 32}, {"hp", "u16", at = 0x20} }
function st.struct(spec, options)
    local layout = setmetatable({ fields = {}, byName = {}, endian = options and options.endian }, Struct)
    local cursor = 0
    for _, entry in ipairs(spec) do
        local field = { name = entry[1], type = entry[2], length = entry[3], count = entry.count, endian = entry.endian }
        field.offset = entry.at or cursor
        field.stride = fieldSize(field)
        local size = field.stride * (field.count or 1)
        cursor = math.max(cursor, field.offset + size)
        if field.type ~= "pad" then
            layout.fields[#layout.fields + 1] = field
            layout.byName[field.name] = field
        end
    end
    layout.size = options and options.size or cursor
    return layout
end

local Record = {}

local function bindField(file, at, field, endian)
    local kind = field.type
    if getmetatable(kind) == Struct then return kind:at(file, at) end
    if textUnits[kind] then return file:text(at, field.length, kind) end
    if kind == "bytes" then
        return st.bind(function() return file:bytes(at, field.length) end, function(value) file:setBytes(at, value) end)
    end
    if kind == "bits" then
        local bits = {}
        for i = 0, field.length - 1 do bits[i] = file:flag(at, i) end
        return bits
    end
    return file:field(at, kind, field.endian or endian)
end

Record.__index = function(record, name)
    local layout = rawget(record, "_layout")
    local field = layout.byName[name]
    if not field then return nil end
    local base = rawget(record, "_offset") + field.offset
    local value
    if field.count then
        value = {}
        for i = 1, field.count do value[i] = bindField(rawget(record, "_file"), base + (i - 1) * field.stride, field, layout.endian) end
    else
        value = bindField(rawget(record, "_file"), base, field, layout.endian)
    end
    rawset(record, name, value)
    return value
end

function Struct:at(file, offset)
    return setmetatable({ _layout = self, _file = file, _offset = offset }, Record)
end

function Struct:array(file, offset, count, stride)
    local list = {}
    for i = 1, count do list[i] = self:at(file, offset + (i - 1) * (stride or self.size)) end
    return list
end

function Struct:names()
    local names = {}
    for i, field in ipairs(self.fields) do names[i] = field.name end
    return names
end
