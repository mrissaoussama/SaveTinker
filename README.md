# SaveTinker

Save editor for Switch and 3DS homebrew. Game support comes from Lua editors installed from repositories.

- [savetinker-editors](https://github.com/mrissaoussama/savetinker-editors): editors for several games and a generic JSON editor
- [savetinker-editor-template](https://github.com/mrissaoussama/savetinker-editor-template): start your own repository, with an example that uses every API call

## Building

| Target | Needs | Output |
|---|---|---|
| `scripts/build.sh pc` | [MSYS2](https://www.msys2.org) UCRT64: gcc, ninja, SDL2, curl | desktop app, `savetinker-cli`, tests |
| `scripts/build.sh switch` | [devkitPro](https://devkitpro.org/wiki/Getting_Started): `switch-dev`, `switch-sdl2`, `switch-curl`, `switch-mesa` | `build/switch/app/SaveTinker.nro` |
| `scripts/build.sh switch2` | [ounce-devkit](https://github.com/ouncebrew/ounce-devkit) installed into devkitPro | `build/switch2/app/SaveTinker.nro` |
| `scripts/build.sh 3ds` | devkitPro: `3ds-dev` | `build/3ds/app/SaveTinker.3dsx` |

The desktop build runs against a folder laid out like a console: `SaveTinker --demo` creates one from `examples/`,
`SaveTinker --console <folder>` uses an existing one.

## Translations

`app/lang/<code>.json` maps English text to the translation. `scripts/update_translations.py` adds new strings.
A `<code>.json` in the data folder's `lang/` overrides the built-in one.

## Command line tool

```sh
savetinker-cli validate my-editor/
savetinker-cli run my-editor/ save/ --set "Player/Gold=999" --out edited/
savetinker-cli diff save/ edited/
savetinker-cli new my-editor/ --id me.game --name "Game" --title 0100ABCD00000000
savetinker-cli pack editors/ repo/ --name "My Editors"
savetinker-cli inspect my-editor/main.lua
```

In an editor, X searches the current page by label and value.

Editors can also be started on the console: mark fields in the hex editor (Y menu) and install the draft from
**Create an editor** on the game screen.

## Editors

An editor is a folder with `manifest.json` and Lua files.

```json
{
  "id": "author.game-name",
  "name": "Game Name Editor",
  "version": "1.0.0",
  "api_version": 1,
  "source": "https://github.com/...",
  "icon": "icon.png",
  "games": [
    {"platform": "switch", "title_id": "0100ABCD00000000", "versions": [">=1.0.0"]},
    {"platform": "3ds", "title_id": "0004000000123400"}
  ]
}
```

- `versions`: `*`, `1.2.0`, `>=`, `>`, `<=`, `<` or `1.0.0 - 1.2.0`. Other versions open after a warning.
- `title_id: "*"`: generic editor for every game on the platform (`platform: "*"` for every console), listed
  separately. Its `detect` decides whether a save fits.
- `platform: "nds"` / `"gba"`: title id is `00000000` + the game code as hex (`BPEE` is `0000000042504545`).
- `api_version`: raised only when a change breaks existing scripts. Older numbers keep working; newer ones are
  refused and marked in repositories. `st.api_version` is the app's number.
- `icon`: PNG or JPEG, 64 KB at most. `game_data`: read access to the game's files.

`main.lua` defines:

- `detect(save)`: true, or `false, "reason"`
- `build(ui, save)`: the screen, run again after every change
- `before_save(save)`: fix checksums and sizes

```lua
function build(ui, save)
    local file = save:file("save.dat")
    ui:page("Player", function(page)
        page:int{ label = "Gold", bind = file:field(0x08, "u32"), max = 999999, step = 100,
                  actions = { { label = "Max", run = function() file:set(0x08, "u32", 999999) end } } }
        page:dropdown{ label = "Class", bind = file:field(0x0E, "u8"), options = { "Knight", "Mage" } }
        local dlc = file:flag(0x0D, 1)
        page:toggle{ label = "Expansion", bind = dlc }
        if dlc:get() then page:int{ label = "Expansion level", bind = file:field(0x30, "u8") } end
    end)
end
```

| Widget | Notes |
|---|---|
| `int`, `float` | `min`, `max`, `step`, `format = "hex"` |
| `text` | length from `file:text(offset, length, encoding)` or `max_length` |
| `toggle` | booleans, bits, or numbers with `on`/`off` values |
| `dropdown`, `segmented`, `tristate` | `options`: `{"a", "b"}`, `{{value=, label=}}` or `{[id] = "label"}` |
| `multiselect` | `bits = {[0] = "Fire", [3] = "Ice"}` |
| `button` | `run`; its writes are one undo step |
| `label`, `note`, `warning`, `separator` | read only |
| `image` | `data` (PNG, JPEG, TGA, BMP) or `pixels`, `width`, `height`, `format` (`rgba8`, `bgra8`, `rgb565`) |
| `section(title or {title=, actions=, collapsed=}, fn)` | group |
| `popup({label=}, fn)` | panel |
| `page(title, fn)` | side bar at top level, inline when nested |
| `ui:state(name, default)` | UI-only value, usable as `bind` |

Every widget takes `label`, `key`, `help`, `readonly`, `on_change` and `actions`. Bindings:
`file:field(offset, type[, endian])`, `file:flag(offset, bit)`, `file:text(offset, length[, encoding])`,
`st.bind(get, set)`. Bit numbers can go past 7: `file:flag(0x100, 137)` is bit 1 of byte 0x111.

Repeated records:

```lua
local Hero = st.struct{ {"level", "u8"}, {"_", "pad", 1}, {"hp", "u16"}, {"name", "utf16", 8},
                        {"stats", "u8", count = 4}, {"pos", Vector}, {"flags", "bits", 16}, {"exp", "u32", at = 0x20} }
for i, hero in ipairs(Hero:array(file, 0x400, 30)) do
    page:int{ label = "Level " .. i, bind = hero.level }
end
```

Fields are bindings (`hero.stats[2]`, `hero.flags[5]`, nested `hero.pos.x`). `Hero.size` is the record size,
`st.struct(spec, {endian = "big", size = 0x40})` sets the byte order or pads the size.

### Lua API

Offsets start at 0. Endian is `"little"`/`"le"` or `"big"`/`"be"`. Binary data is a Lua string.

| Call | |
|---|---|
| `st.platform.id`, `.name` | Console |
| `st.game.title_id`, `.name`, `.version`, `.platform`, `.save_kind` | Game and save (`account`, `device`, `extdata`, `ds-card`) |
| `st.game.dlc`, `st.game.has_dlc(id)` | Installed add-ons |
| `st.game.version_is(pattern)`, `st.version_matches(pattern, version)` | Version checks |
| `st.editor.id`, `.name`, `.version` | Running editor |
| `save:files()`, `:has(name)`, `:file(name)` | Save contents |
| `file:u8` ... `file:f64(offset[, endian])`, `file:get`, `file:set(offset, type, value[, endian])` | Numbers |
| `file:bit`, `file:setBit(offset, bit, value)` | Bits |
| `file:bytes(offset, length)`, `file:setBytes(offset, data)`, `file:find(data[, from])` | Bytes |
| `file:str`, `file:setStr(offset, length, text[, encoding])` | `utf8`, `utf16`, `utf16le`, `utf16be` |
| `file:splice(offset, length, data)`, `file:content()`, `file:setContent(data)`, `file:size()` | Resizing |
| `st.transaction(label, fn)` | One undo step, rolled back on error |
| `st.hash.crc32/crc16/adler32/murmur3/md5/sha1/sha256(data)`, `st.hash.hmac_sha256(key, data)` | Hashes |
| `st.zlib`, `st.deflate`, `st.gzip`, `st.zstd`: `.compress(data[, level])`, `.decompress(data)` | Compression |
| `st.crypto.aes_encrypt/aes_decrypt(mode, key, data[, iv])` | `ecb`, `cbc`, `ctr`; no padding |
| `st.crypto.aes_cmac(key, data)`, `st.crypto.xor(data, key)`, `st.hash.hmac_sha1(key, data)` | |
| `st.crypto.pbkdf2(hash, password, salt, iterations, length)` | `sha1` or `sha256` |
| `st.format.sarc/byml/msbt(data)` | Nintendo archives and messages |
| `st.hex`, `st.unhex`, `st.json.decode/encode`, `st.json.null` | |
| `require(name)` | `name.lua` from the editor (`a.b` is `a/b.lua`), then the shared `_lib` folder |
| `require("st.gvas")` | Unreal saves: `parse(file)` gives a property tree, `bind(file, node)` edits a value |
| `require("st.es3")` | Easy Save 3 (Unity): `decode(data[, password])`, `encode(text[, password, {gzip = true}])` |
| `st.data(path)`, `st.labels(path)` | Editor files; `labels` tries `<dir>/<lang>/<file>` first |
| `st.game_data.read(path[, optional])`, `st.game_data.list(dir)` | Game files, with `"game_data": true` |
| `print(...)`, `st.warn(...)` | Editor log (- button) |

`string.pack`/`string.unpack` are available; `io`, `os`, `debug`, `load` and `package` are not. Scripts have
memory and instruction limits and no network access.

## Save files

GBA and DS saves (`.sav`, `.srm`, `.sa1`, `.dsv`) in the folders set in Settings appear as games when a ROM with
the same name sits next to them. Editors see them as `save.bin`.

Backups are made before the first write, and writing stops if the SD card lacks room for them.

## Repositories

A repository is a `repo.json` anywhere on the web; `savetinker-cli pack` writes it.

```json
{
  "name": "My Editors",
  "editors": [
    {
      "id": "author.game-name",
      "name": "Game Name Editor",
      "version": "1.0.0",
      "api_version": 1,
      "games": [{"platform": "switch", "title_id": "0100ABCD00000000"}],
      "download": "editors/game-name-1.0.0.zip",
      "sha256": "...",
      "size": 12345
    }
  ]
}
```

Relative `download` links resolve against the `repo.json` URL.
