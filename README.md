# SaveTinker

Save editor homebrew for Nintendo Switch (3DS later). Game support comes from small Lua editors that can be
installed from any number of repositories.

## Building

Needs devkitPro (devkitA64, libnx, `switch-dev`) and, for the PC build and tests, MSYS2 UCRT64 GCC + Ninja.

```sh
scripts/build.sh pc       # host build + unit tests
scripts/build.sh switch   # build/switch/app/switch/SaveTinker.nro
```

Send to a console with `nxlink -s build/switch/app/switch/SaveTinker.nro`.

## Layout

| Path | Contents |
|---|---|
| `core/` | Platform-independent library: documents and undo, Lua host, manifests, repos, backups |
| `platform/` | `pc/` folder-backed console for testing, `switch/` libnx backend |
| `app/switch/` | Switch front end |
| `examples/editors/` | Example editors |
| `tests/` | doctest unit tests |

## Editors

An editor is a folder with `manifest.json` and Lua files.

```json
{
  "id": "author.game-name",
  "name": "Game Name Editor",
  "version": "1.0.0",
  "api_version": 1,
  "source": "https://github.com/...",
  "games": [
    {"platform": "switch", "title_id": "0100ABCD00000000", "versions": [">=1.0.0"]},
    {"platform": "3ds", "title_id": "0004000000123400"}
  ]
}
```

`versions` accepts `*`, exact versions, `>=`, `>`, `<=`, `<` and ranges like `1.0.0 - 1.2.0`.

The entry script (`main.lua` by default) can define:

- `detect(save)` returns true when the save matches what the editor understands
- `before_save(save)` runs before writing, typically to fix checksums

### Lua API

Offsets are zero-based. Endian arguments are `"little"`/`"le"` or `"big"`/`"be"`.

| Call | Description |
|---|---|
| `st.platform.id`, `st.platform.name` | Console the game runs on |
| `st.game.title_id`, `.name`, `.version`, `.platform` | Game being edited |
| `st.editor.id`, `.name`, `.version` | Running editor |
| `st.save:files()`, `:has(name)`, `:file(name)` | Save contents |
| `file:u8/u16/u32/u64/s8/s16/s32/s64/f32/f64(offset[, endian])` | Typed reads |
| `file:get(offset, type[, endian])`, `file:set(offset, type, value[, endian])` | Generic access, writes are range checked |
| `file:bit(offset, bit)`, `file:setBit(offset, bit, value)` | Single bits |
| `file:bytes(offset, length)`, `file:setBytes(offset, data)` | Raw bytes as Lua strings |
| `file:splice(offset, length, data)`, `file:content()`, `file:setContent(data)` | Resizing edits |
| `file:str(offset, length[, encoding])`, `file:setStr(offset, length, text[, encoding])` | `utf8`, `utf16`, `utf16le`, `utf16be` |
| `file:find(data[, from])`, `file:size()`, `file:endian([endian])` | Search, size, default byte order |
| `st.transaction(label, fn)` | Groups writes into one undo step, rolled back on error |
| `st.hash.crc32/crc16/adler32/md5/sha1/sha256(data)` | Digests return raw bytes |
| `st.hex(data)`, `st.unhex(text)` | Hex conversion |
| `st.json.decode(text)`, `st.json.encode(value[, pretty])`, `st.json.null` | JSON |
| `require(name)` | Loads `name.lua` from the editor folder, then the shared library folder |
| `print(...)`, `st.warn(...)` | Editor log |

`string.pack`/`string.unpack` are available for packed structures. `io`, `os`, `debug`, `load` and `package`
are not. Scripts run with memory and instruction limits.

## Repositories

A repository is a `repo.json` served from anywhere. Users can add as many as they like.

```json
{
  "name": "My Editors",
  "maintainer": "someone",
  "editors": [
    {
      "id": "author.game-name",
      "name": "Game Name Editor",
      "version": "1.0.0",
      "games": [{"platform": "switch", "title_id": "0100ABCD00000000"}],
      "download": "editors/game-name-1.0.0.zip",
      "sha256": "...",
      "size": 12345
    }
  ]
}
```

Relative `download` links resolve against the `repo.json` URL.
