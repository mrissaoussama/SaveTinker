#include <limits>
#include <new>
#include <optional>
#include <string>

#include "lua_support.hpp"

namespace st::lua {

namespace {

constexpr const char* kSaveType = "SaveTinker.Save";
constexpr const char* kFileType = "SaveTinker.File";

struct SaveRef {
    Document* document;
};

struct FileRef {
    Document* document;
    std::string name;
    Endian endian;
};

enum class FieldKind { U8, U16, U32, U64, S8, S16, S32, S64, F32, F64 };

std::optional<FieldKind> parseFieldKind(std::string_view name) {
    static constexpr std::pair<std::string_view, FieldKind> names[] = {
        {"u8", FieldKind::U8},   {"u16", FieldKind::U16}, {"u32", FieldKind::U32}, {"u64", FieldKind::U64},
        {"s8", FieldKind::S8},   {"s16", FieldKind::S16}, {"s32", FieldKind::S32}, {"s64", FieldKind::S64},
        {"i8", FieldKind::S8},   {"i16", FieldKind::S16}, {"i32", FieldKind::S32}, {"i64", FieldKind::S64},
        {"f32", FieldKind::F32}, {"f64", FieldKind::F64}, {"float", FieldKind::F32}, {"double", FieldKind::F64},
    };
    for (const auto& [text, kind] : names)
        if (text == name) return kind;
    return std::nullopt;
}

FieldKind checkFieldKind(lua_State* L, int index) {
    const auto kind = parseFieldKind(checkString(L, index));
    if (!kind) luaL_argerror(L, index, "unknown field type");
    return *kind;
}

Endian optEndian(lua_State* L, int index, Endian fallback) {
    if (lua_isnoneornil(L, index)) return fallback;
    const auto name = checkString(L, index);
    if (name == "le" || name == "little") return Endian::Little;
    if (name == "be" || name == "big") return Endian::Big;
    luaL_argerror(L, index, "endian must be \"little\" or \"big\"");
    return fallback;
}

SaveRef& checkSave(lua_State* L, int index) {
    return *static_cast<SaveRef*>(luaL_checkudata(L, index, kSaveType));
}

FileRef& checkFile(lua_State* L, int index) {
    return *static_cast<FileRef*>(luaL_checkudata(L, index, kFileType));
}

const ByteBuffer& bufferOf(const FileRef& file) {
    return file.document->file(file.name);
}

void pushFile(lua_State* L, Document& document, const std::string& name) {
    if (!document.hasFile(name)) luaL_error(L, "save has no file named '%s'", name.c_str());
    void* memory = lua_newuserdatauv(L, sizeof(FileRef), 0);
    new (memory) FileRef{&document, name, Endian::Little};
    luaL_setmetatable(L, kFileType);
}

void pushField(lua_State* L, const ByteBuffer& buffer, std::size_t offset, FieldKind kind, Endian endian) {
    switch (kind) {
    case FieldKind::U8: lua_pushinteger(L, buffer.read<std::uint8_t>(offset, endian)); break;
    case FieldKind::U16: lua_pushinteger(L, buffer.read<std::uint16_t>(offset, endian)); break;
    case FieldKind::U32: lua_pushinteger(L, buffer.read<std::uint32_t>(offset, endian)); break;
    case FieldKind::U64: lua_pushinteger(L, static_cast<lua_Integer>(buffer.read<std::uint64_t>(offset, endian))); break;
    case FieldKind::S8: lua_pushinteger(L, buffer.read<std::int8_t>(offset, endian)); break;
    case FieldKind::S16: lua_pushinteger(L, buffer.read<std::int16_t>(offset, endian)); break;
    case FieldKind::S32: lua_pushinteger(L, buffer.read<std::int32_t>(offset, endian)); break;
    case FieldKind::S64: lua_pushinteger(L, buffer.read<std::int64_t>(offset, endian)); break;
    case FieldKind::F32: lua_pushnumber(L, buffer.readFloat(offset, endian)); break;
    case FieldKind::F64: lua_pushnumber(L, buffer.readDouble(offset, endian)); break;
    }
}

template <std::integral T>
T checkInRange(lua_State* L, int index, const char* typeName) {
    const lua_Integer value = luaL_checkinteger(L, index);
    if constexpr (sizeof(T) < sizeof(lua_Integer)) {
        if (value < static_cast<lua_Integer>(std::numeric_limits<T>::min()) ||
            value > static_cast<lua_Integer>(std::numeric_limits<T>::max()))
            luaL_error(L, "value %I out of range for %s", value, typeName);
    }
    return static_cast<T>(value);
}

void writeField(lua_State* L, FileRef& file, std::size_t offset, FieldKind kind, int valueIndex, Endian endian) {
    Document& document = *file.document;
    switch (kind) {
    case FieldKind::U8: document.writeValue(file.name, offset, checkInRange<std::uint8_t>(L, valueIndex, "u8"), endian); break;
    case FieldKind::U16: document.writeValue(file.name, offset, checkInRange<std::uint16_t>(L, valueIndex, "u16"), endian); break;
    case FieldKind::U32: document.writeValue(file.name, offset, checkInRange<std::uint32_t>(L, valueIndex, "u32"), endian); break;
    case FieldKind::U64: document.writeValue(file.name, offset, static_cast<std::uint64_t>(luaL_checkinteger(L, valueIndex)), endian); break;
    case FieldKind::S8: document.writeValue(file.name, offset, checkInRange<std::int8_t>(L, valueIndex, "s8"), endian); break;
    case FieldKind::S16: document.writeValue(file.name, offset, checkInRange<std::int16_t>(L, valueIndex, "s16"), endian); break;
    case FieldKind::S32: document.writeValue(file.name, offset, checkInRange<std::int32_t>(L, valueIndex, "s32"), endian); break;
    case FieldKind::S64: document.writeValue(file.name, offset, static_cast<std::int64_t>(luaL_checkinteger(L, valueIndex)), endian); break;
    case FieldKind::F32: {
        ByteBuffer scratch(Bytes(4));
        scratch.writeFloat(0, static_cast<float>(luaL_checknumber(L, valueIndex)), endian);
        document.write(file.name, offset, scratch.view());
        break;
    }
    case FieldKind::F64: {
        ByteBuffer scratch(Bytes(8));
        scratch.writeDouble(0, luaL_checknumber(L, valueIndex), endian);
        document.write(file.name, offset, scratch.view());
        break;
    }
    }
}

template <FieldKind Kind>
int fileReadTyped(lua_State* L) {
    auto& file = checkFile(L, 1);
    pushField(L, bufferOf(file), checkOffset(L, 2), Kind, optEndian(L, 3, file.endian));
    return 1;
}

int fileGet(lua_State* L) {
    auto& file = checkFile(L, 1);
    pushField(L, bufferOf(file), checkOffset(L, 2), checkFieldKind(L, 3), optEndian(L, 4, file.endian));
    return 1;
}

int fileSet(lua_State* L) {
    auto& file = checkFile(L, 1);
    writeField(L, file, checkOffset(L, 2), checkFieldKind(L, 3), 4, optEndian(L, 5, file.endian));
    return 0;
}

int fileSize(lua_State* L) {
    lua_pushinteger(L, static_cast<lua_Integer>(bufferOf(checkFile(L, 1)).size()));
    return 1;
}

int fileName(lua_State* L) {
    lua_pushstring(L, checkFile(L, 1).name.c_str());
    return 1;
}

int fileEndian(lua_State* L) {
    auto& file = checkFile(L, 1);
    if (lua_isnoneornil(L, 2)) {
        lua_pushstring(L, file.endian == Endian::Little ? "little" : "big");
        return 1;
    }
    file.endian = optEndian(L, 2, file.endian);
    lua_settop(L, 1);
    return 1;
}

int fileBit(lua_State* L) {
    auto& file = checkFile(L, 1);
    const auto bit = luaL_checkinteger(L, 3);
    luaL_argcheck(L, bit >= 0 && bit <= 7, 3, "bit must be 0-7");
    lua_pushboolean(L, bufferOf(file).readBit(checkOffset(L, 2), static_cast<unsigned>(bit)));
    return 1;
}

int fileSetBit(lua_State* L) {
    auto& file = checkFile(L, 1);
    const auto offset = checkOffset(L, 2);
    const auto bit = luaL_checkinteger(L, 3);
    luaL_argcheck(L, bit >= 0 && bit <= 7, 3, "bit must be 0-7");
    ByteBuffer scratch(bufferOf(file).readBytes(offset, 1));
    scratch.writeBit(0, static_cast<unsigned>(bit), lua_toboolean(L, 4) != 0);
    file.document->write(file.name, offset, scratch.view());
    return 0;
}

int fileBytes(lua_State* L) {
    auto& file = checkFile(L, 1);
    const auto offset = checkOffset(L, 2);
    const auto length = checkOffset(L, 3);
    pushBytes(L, bufferOf(file).readBytes(offset, length));
    return 1;
}

int fileSetBytes(lua_State* L) {
    auto& file = checkFile(L, 1);
    file.document->write(file.name, checkOffset(L, 2), asBytes(checkString(L, 3)));
    return 0;
}

int fileSplice(lua_State* L) {
    auto& file = checkFile(L, 1);
    file.document->splice(file.name, checkOffset(L, 2), checkOffset(L, 3), asBytes(checkString(L, 4)));
    return 0;
}

int fileContent(lua_State* L) {
    pushBytes(L, bufferOf(checkFile(L, 1)).view());
    return 1;
}

int fileSetContent(lua_State* L) {
    auto& file = checkFile(L, 1);
    const auto data = asBytes(checkString(L, 2));
    file.document->setContent(file.name, Bytes(data.begin(), data.end()));
    return 0;
}

enum class TextEncoding { Utf8, Utf16Le, Utf16Be };

TextEncoding optEncoding(lua_State* L, int index, Endian fileEndian) {
    if (lua_isnoneornil(L, index)) return TextEncoding::Utf8;
    const auto name = checkString(L, index);
    if (name == "utf8") return TextEncoding::Utf8;
    if (name == "utf16") return fileEndian == Endian::Big ? TextEncoding::Utf16Be : TextEncoding::Utf16Le;
    if (name == "utf16le") return TextEncoding::Utf16Le;
    if (name == "utf16be") return TextEncoding::Utf16Be;
    luaL_argerror(L, index, "encoding must be utf8, utf16, utf16le or utf16be");
    return TextEncoding::Utf8;
}

std::string readText(const ByteBuffer& buffer, std::size_t offset, std::size_t length, TextEncoding encoding) {
    if (encoding == TextEncoding::Utf8) return buffer.readUtf8(offset, length);
    return buffer.readUtf16(offset, length, encoding == TextEncoding::Utf16Be ? Endian::Big : Endian::Little);
}

void writeText(Document& document, const std::string& file, std::size_t offset, std::size_t length,
               std::string_view text, TextEncoding encoding) {
    const std::size_t byteLength = encoding == TextEncoding::Utf8 ? length : length * 2;
    ByteBuffer scratch{Bytes(byteLength)};
    if (encoding == TextEncoding::Utf8) scratch.writeUtf8(0, text, length);
    else scratch.writeUtf16(0, text, length, encoding == TextEncoding::Utf16Be ? Endian::Big : Endian::Little);
    document.write(file, offset, scratch.view());
}

int fileStr(lua_State* L) {
    auto& file = checkFile(L, 1);
    const auto offset = checkOffset(L, 2);
    const auto length = checkOffset(L, 3);
    const auto text = readText(bufferOf(file), offset, length, optEncoding(L, 4, file.endian));
    lua_pushlstring(L, text.data(), text.size());
    return 1;
}

int fileSetStr(lua_State* L) {
    auto& file = checkFile(L, 1);
    const auto offset = checkOffset(L, 2);
    const auto length = checkOffset(L, 3);
    const auto text = checkString(L, 4);
    writeText(*file.document, file.name, offset, length, text, optEncoding(L, 5, file.endian));
    return 0;
}

constexpr const char* kBindingType = "SaveTinker.Binding";

struct BindingRef {
    enum class Shape { Number, Bit, Text };
    Document* document;
    std::string file;
    std::size_t offset;
    Shape shape;
    FieldKind kind = FieldKind::U8;
    Endian endian = Endian::Little;
    unsigned bit = 0;
    std::size_t length = 0;
    TextEncoding encoding = TextEncoding::Utf8;
};

BindingRef& checkBinding(lua_State* L, int index) {
    return *static_cast<BindingRef*>(luaL_checkudata(L, index, kBindingType));
}

void pushBinding(lua_State* L, BindingRef binding) {
    if (!binding.document->hasFile(binding.file)) luaL_error(L, "save has no file named '%s'", binding.file.c_str());
    void* memory = lua_newuserdatauv(L, sizeof(BindingRef), 0);
    new (memory) BindingRef(std::move(binding));
    luaL_setmetatable(L, kBindingType);
}

int fileField(lua_State* L) {
    auto& file = checkFile(L, 1);
    BindingRef binding{file.document, file.name, checkOffset(L, 2), BindingRef::Shape::Number};
    binding.kind = checkFieldKind(L, 3);
    binding.endian = optEndian(L, 4, file.endian);
    pushBinding(L, std::move(binding));
    return 1;
}

int fileFlag(lua_State* L) {
    auto& file = checkFile(L, 1);
    BindingRef binding{file.document, file.name, checkOffset(L, 2), BindingRef::Shape::Bit};
    const auto bit = luaL_checkinteger(L, 3);
    luaL_argcheck(L, bit >= 0 && bit <= 7, 3, "bit must be 0-7");
    binding.bit = static_cast<unsigned>(bit);
    pushBinding(L, std::move(binding));
    return 1;
}

int fileText(lua_State* L) {
    auto& file = checkFile(L, 1);
    BindingRef binding{file.document, file.name, checkOffset(L, 2), BindingRef::Shape::Text};
    binding.length = checkOffset(L, 3);
    binding.encoding = optEncoding(L, 4, file.endian);
    pushBinding(L, std::move(binding));
    return 1;
}

int bindingGet(lua_State* L) {
    auto& binding = checkBinding(L, 1);
    const auto& buffer = binding.document->file(binding.file);
    switch (binding.shape) {
    case BindingRef::Shape::Number: pushField(L, buffer, binding.offset, binding.kind, binding.endian); break;
    case BindingRef::Shape::Bit: lua_pushboolean(L, buffer.readBit(binding.offset, binding.bit)); break;
    case BindingRef::Shape::Text: {
        const auto text = readText(buffer, binding.offset, binding.length, binding.encoding);
        lua_pushlstring(L, text.data(), text.size());
        break;
    }
    }
    return 1;
}

int bindingSet(lua_State* L) {
    auto& binding = checkBinding(L, 1);
    switch (binding.shape) {
    case BindingRef::Shape::Number: {
        FileRef file{binding.document, binding.file, binding.endian};
        writeField(L, file, binding.offset, binding.kind, 2, binding.endian);
        break;
    }
    case BindingRef::Shape::Bit: {
        ByteBuffer scratch(binding.document->file(binding.file).readBytes(binding.offset, 1));
        scratch.writeBit(0, binding.bit, lua_toboolean(L, 2) != 0);
        binding.document->write(binding.file, binding.offset, scratch.view());
        break;
    }
    case BindingRef::Shape::Text:
        writeText(*binding.document, binding.file, binding.offset, binding.length, checkString(L, 2), binding.encoding);
        break;
    }
    return 0;
}

int bindingToString(lua_State* L) {
    auto& binding = checkBinding(L, 1);
    lua_pushfstring(L, "Binding(%s@%I)", binding.file.c_str(), static_cast<lua_Integer>(binding.offset));
    return 1;
}

int bindingCollect(lua_State* L) {
    checkBinding(L, 1).~BindingRef();
    return 0;
}

int fileFind(lua_State* L) {
    auto& file = checkFile(L, 1);
    const auto pattern = asBytes(checkString(L, 2));
    const auto from = lua_isnoneornil(L, 3) ? 0 : checkOffset(L, 3);
    if (auto found = bufferOf(file).find(pattern, from)) lua_pushinteger(L, static_cast<lua_Integer>(*found));
    else lua_pushnil(L);
    return 1;
}

int fileToString(lua_State* L) {
    lua_pushfstring(L, "File(%s)", checkFile(L, 1).name.c_str());
    return 1;
}

int fileCollect(lua_State* L) {
    checkFile(L, 1).~FileRef();
    return 0;
}

int saveFiles(lua_State* L) {
    const auto names = checkSave(L, 1).document->fileNames();
    lua_createtable(L, static_cast<int>(names.size()), 0);
    for (std::size_t i = 0; i < names.size(); ++i) {
        lua_pushstring(L, names[i].c_str());
        lua_rawseti(L, -2, static_cast<lua_Integer>(i + 1));
    }
    return 1;
}

int saveHas(lua_State* L) {
    lua_pushboolean(L, checkSave(L, 1).document->hasFile(std::string(checkString(L, 2))));
    return 1;
}

int saveFile(lua_State* L) {
    pushFile(L, *checkSave(L, 1).document, std::string(checkString(L, 2)));
    return 1;
}

void registerType(lua_State* L, const char* name, const luaL_Reg* methods, const luaL_Reg* metamethods) {
    luaL_newmetatable(L, name);
    luaL_setfuncs(L, metamethods, 0);
    lua_newtable(L);
    luaL_setfuncs(L, methods, 0);
    lua_setfield(L, -2, "__index");
    lua_pushliteral(L, "locked");
    lua_setfield(L, -2, "__metatable");
    lua_pop(L, 1);
}

}

void registerSaveTypes(lua_State* L) {
    static const luaL_Reg fileMethods[] = {
        {"size", guarded<fileSize>},
        {"name", guarded<fileName>},
        {"endian", guarded<fileEndian>},
        {"u8", guarded<fileReadTyped<FieldKind::U8>>},
        {"u16", guarded<fileReadTyped<FieldKind::U16>>},
        {"u32", guarded<fileReadTyped<FieldKind::U32>>},
        {"u64", guarded<fileReadTyped<FieldKind::U64>>},
        {"s8", guarded<fileReadTyped<FieldKind::S8>>},
        {"s16", guarded<fileReadTyped<FieldKind::S16>>},
        {"s32", guarded<fileReadTyped<FieldKind::S32>>},
        {"s64", guarded<fileReadTyped<FieldKind::S64>>},
        {"f32", guarded<fileReadTyped<FieldKind::F32>>},
        {"f64", guarded<fileReadTyped<FieldKind::F64>>},
        {"get", guarded<fileGet>},
        {"set", guarded<fileSet>},
        {"bit", guarded<fileBit>},
        {"setBit", guarded<fileSetBit>},
        {"bytes", guarded<fileBytes>},
        {"setBytes", guarded<fileSetBytes>},
        {"splice", guarded<fileSplice>},
        {"content", guarded<fileContent>},
        {"setContent", guarded<fileSetContent>},
        {"str", guarded<fileStr>},
        {"setStr", guarded<fileSetStr>},
        {"find", guarded<fileFind>},
        {"field", guarded<fileField>},
        {"flag", guarded<fileFlag>},
        {"text", guarded<fileText>},
        {nullptr, nullptr},
    };
    static const luaL_Reg fileMeta[] = {
        {"__gc", fileCollect},
        {"__tostring", fileToString},
        {nullptr, nullptr},
    };
    registerType(L, kFileType, fileMethods, fileMeta);

    static const luaL_Reg saveMethods[] = {
        {"files", guarded<saveFiles>},
        {"has", guarded<saveHas>},
        {"file", guarded<saveFile>},
        {nullptr, nullptr},
    };
    static const luaL_Reg saveMeta[] = {{nullptr, nullptr}};
    registerType(L, kSaveType, saveMethods, saveMeta);

    static const luaL_Reg bindingMethods[] = {
        {"get", guarded<bindingGet>},
        {"set", guarded<bindingSet>},
        {nullptr, nullptr},
    };
    static const luaL_Reg bindingMeta[] = {
        {"__gc", bindingCollect},
        {"__tostring", bindingToString},
        {nullptr, nullptr},
    };
    registerType(L, kBindingType, bindingMethods, bindingMeta);
}

std::optional<FieldBindingInfo> fieldBindingInfo(lua_State* L, int index) {
    auto* binding = static_cast<BindingRef*>(luaL_testudata(L, index, kBindingType));
    if (!binding) return std::nullopt;
    FieldBindingInfo info;
    switch (binding->shape) {
    case BindingRef::Shape::Bit: info.type = FieldBindingInfo::Type::Bool; break;
    case BindingRef::Shape::Text:
        info.type = FieldBindingInfo::Type::Text;
        info.maxLength = binding->length;
        break;
    case BindingRef::Shape::Number: {
        const bool isFloat = binding->kind == FieldKind::F32 || binding->kind == FieldKind::F64;
        info.type = isFloat ? FieldBindingInfo::Type::Float : FieldBindingInfo::Type::Integer;
        switch (binding->kind) {
        case FieldKind::U8: info.min = 0; info.max = 0xFF; break;
        case FieldKind::U16: info.min = 0; info.max = 0xFFFF; break;
        case FieldKind::U32: info.min = 0; info.max = 0xFFFFFFFFu; break;
        case FieldKind::S8: info.min = -128; info.max = 127; break;
        case FieldKind::S16: info.min = -32768; info.max = 32767; break;
        case FieldKind::S32: info.min = -2147483648.0; info.max = 2147483647.0; break;
        default: break;
        }
        break;
    }
    }
    return info;
}

void pushSave(lua_State* L, Document& document) {
    void* memory = lua_newuserdatauv(L, sizeof(SaveRef), 0);
    new (memory) SaveRef{&document};
    luaL_setmetatable(L, kSaveType);
}

}
