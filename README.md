# SaveTinker

Save editor homebrew for Nintendo Switch (3DS later). Game support comes from small Lua editors that can be
installed from any number of repositories.

## Building

Needs devkitPro (devkitA64, libnx, `switch-dev`, `switch-sdl2`, `switch-curl`, `switch-mesa`) and, for the
desktop build and tests, MSYS2 UCRT64 GCC, Ninja, SDL2 and curl.

```sh
scripts/build.sh pc       # desktop build + unit tests
scripts/build.sh switch   # build/switch/app/SaveTinker.nro
scripts/build.sh switch2  # Switch 2 build, needs ounce-devkit installed into devkitPro
scripts/build.sh 3ds      # build/3ds/app/SaveTinker.3dsx, needs 3ds-dev
```

Send to a console with `nxlink -s build/switch/app/SaveTinker.nro`.

The desktop build runs the same UI against a folder that acts as a console:

```sh
build/pc/app/SaveTinker --demo              # creates ./demo-console from examples/
build/pc/app/SaveTinker --console <folder>
```

Keyboard on desktop: arrows move, Enter selects, Esc goes back, Q/E switch tabs or pages, X/Y are the
X/Y buttons, Ctrl+Z/Ctrl+Y undo and redo, Ctrl+S writes the save.

### UI testing

Configure with `-DSAVETINKER_AUTOPILOT=ON` and place `autopilot.txt` in the data folder
(`sdmc:/switch/SaveTinker/` on Switch, `<console>/SaveTinker/` on desktop). It replays button presses and
saves screenshots to `screenshots/`, which also works inside emulators:

```
wait 60
press DOWN 2
press A
shot game-screen
quit
```

`type <text>` enters text into an open prompt and `press ENTER` confirms it.

## Translations

Interface text is looked up by its English wording in `app/lang/<code>.json`, which is built into the app.
`scripts/update_translations.py [code]` collects every `tr`/`trf` string from `app/` and adds missing ones
with an empty value (shown in English until translated). Users can override or add a language without
rebuilding by placing `<code>.json` in the `lang/` folder of the data directory. Translations whose `{}`
placeholders do not match the English text are ignored.

The language setting also picks editor label files (`st.labels`), see below.

## Command line tool

The desktop build also produces `savetinker-cli` for editor authors and repository maintainers:

```sh
savetinker-cli validate my-editor/                     # Lua syntax, JSON files, manifest
savetinker-cli run my-editor/ save-folder/             # prints every page as text
savetinker-cli run my-editor/ save.dat --set "Player/Gold=999" --out edited/
savetinker-cli diff save-folder/ edited/               # byte ranges that differ
savetinker-cli new my-editor/ --id me.game --name "Game" --title 0100ABCD00000000
savetinker-cli pack editors/ site/ --name "My Editors" # zips + repo.json with hashes
savetinker-cli inspect my-editor/main.lua              # writes, modules and data files
```

`pack` keeps the repository name and changelogs from an existing `repo.json` and refuses to write it when
any editor has errors.

## Editor creator

Without a computer, an editor can be started from the hex editor: open its menu (Y), choose
**Mark field here** for each value, and optionally **Use bytes here as save signature** so the editor only
opens matching saves. **Editor creator** (also on the game screen) shows the draft, previews the generated
Lua and installs it. Drafts are kept in `drafts/<title id>.json` and can be turned into an editor folder
with `savetinker-cli new <folder> --template drafts/<title id>.json`.

## Layout

| Path | Contents |
|---|---|
| `core/` | Platform-independent library: documents and undo, Lua host, manifests, repos, backups |
| `platform/` | `pc/` folder-backed console for testing, `switch/` libnx backend |
| `app/` | Dear ImGui front end shared by Switch and desktop |
| `net/` | libcurl HTTP client |
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

`versions` accepts `*`, exact versions, `>=`, `>`, `<=`, `<` and ranges like `1.0.0 - 1.2.0`. When the installed game
version is outside the list, the app warns and lets the user open the editor anyway; scripts that know a
version breaks them can refuse in `detect`. On 3DS, editors can also target `"platform": "nds"` for DS cartridges
(title id `00000000` + the game code as hex) and receive extra data as its own save (`st.game.save_kind == "extdata"`).

The entry script (`main.lua` by default) can define:

- `detect(save)` returns true when the save matches what the editor understands, or `false, "reason"` to explain
- `build(ui, save)` describes the editor screen
- `before_save(save)` runs before writing, typically to fix checksums

### Building the UI

`build` runs again after every change, so plain Lua decides what is shown. Pages only run when opened.

```lua
function build(ui, save)
    local file = save:file("save.dat")
    ui:page("Player", function(page)
        page:int{ label = "Gold", bind = file:field(0x08, "u32"), max = 999999, step = 100,
                  actions = { { label = "Max", run = function() file:set(0x08, "u32", 999999) end } } }
        page:dropdown{ label = "Class", bind = file:field(0x0E, "u8"), options = { "Knight", "Mage" },
                       on_change = function(class) file:set(0x48, "u8", class == 1 and 80 or 20) end }
        local dlc = file:flag(0x0D, 1)
        page:toggle{ label = "Expansion", bind = dlc }
        if dlc:get() then
            page:int{ label = "Expansion level", bind = file:field(0x30, "u8") }
        end
    end)
end
```

| Widget | Notes |
|---|---|
| `int`, `float` | `min`, `max`, `step`, `format = "hex"`; ranges default to the field type |
| `text` | length comes from `file:text(offset, length, encoding)` or `max_length` |
| `toggle` | booleans, bits, or numbers with `on`/`off` values |
| `dropdown`, `segmented`, `tristate` | `options`: `{"a", "b"}` (values 0, 1...), `{{value=, label=}}` or `{[id] = "label"}` |
| `multiselect` | bit flags: `bits = {[0] = "Fire", [3] = "Ice"}` |
| `button` | `run` function; all writes inside form one undo step |
| `label`, `note`, `warning`, `separator` | read only |
| `section(title or {title=, actions=, collapsed=}, fn)` | grouped widgets with optional buttons |
| `popup({label=}, fn)` | opens a panel |
| `page(title, fn)` | top level pages appear in the side bar; nested pages open inline |
| `ui:state(name, default)` | UI-only value that survives rebuilds, usable as `bind` |

Every widget accepts `label`, `key`, `help`, `readonly`, `on_change` and `actions`. Bindings are
`file:field(offset, type[, endian])`, `file:flag(offset, bit)`, `file:text(offset, length[, encoding])` or
`st.bind(getter, setter)`.

### Lua API

Offsets are zero-based. Endian arguments are `"little"`/`"le"` or `"big"`/`"be"`.

| Call | Description |
|---|---|
| `st.platform.id`, `st.platform.name` | Console the game runs on |
| `st.game.title_id`, `.name`, `.version`, `.platform` | Game being edited |
| `st.game.dlc`, `st.game.has_dlc(id)` | Installed add-on content ids (a suffix of the id is enough) |
| `st.game.version_is(pattern)`, `st.version_matches(pattern, version)` | Version checks, same patterns as the manifest |
| `st.game.save_kind` | `account`, `device`, `extdata`, `ds-card`... |
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
| `st.hash.murmur3(data[, seed])` | 32-bit MurmurHash3, common for hashed names |
| `st.zstd.decompress(data[, dictionaries])` | zstd, picking the dictionary the frame asks for |
| `st.format.sarc(data)`, `st.format.byml(data)`, `st.format.msbt(data[, raw])` | Nintendo archives, BYML documents and message files |
| `st.hex(data)`, `st.unhex(text)` | Hex conversion |
| `st.json.decode(text)`, `st.json.encode(value[, pretty])`, `st.json.null` | JSON |
| `require(name)` | Loads `name.lua` from the editor folder, then the shared library folder |
| `st.data(path)`, `st.labels(path)` | Files shipped with the editor; `labels` tries `<dir>/<lang>/<file>` first |
| `st.lang` | Preferred label language |
| `print(...)`, `st.warn(...)` | Editor log |

### Game files

Editors that need tables from the game itself (item names, stats) add `"game_data": true` to
`manifest.json` and read the game's RomFS, read only:

```lua
local text = st.game_data.read("text/items.txt")          -- error if missing
local maybe = st.game_data.read("text/extra.txt", true)   -- nil if missing
for _, entry in ipairs(st.game_data.list("text")) do print(entry.name, entry.size, entry.directory) end
```

The code inspector flags editors that use this. On Switch the RomFS of the installed game is mounted on
first use; on desktop the files come from `<console>/romfs/<title id>/`, and `savetinker-cli run` takes
`--game-data <folder>`.

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
