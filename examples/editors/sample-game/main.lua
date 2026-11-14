local layout = require("layout")

function detect(save)
    if not save:has("save.dat") then return false end
    return save:file("save.dat"):bytes(0, 4) == layout.magic
end

function before_save(save)
    local file = save:file("save.dat")
    file:set(layout.checksum, "u32", layout.checksumOf(file))
end

local function player(page, file)
    page:text{ label = "Name", bind = file:text(layout.name, 16, "utf16"), help = "Shown on the title screen" }
    page:int{
        label = "Gold", bind = file:field(layout.gold, "u32"), max = 999999, step = 100,
        actions = { { label = "Max", run = function() file:set(layout.gold, "u32", 999999) end } },
    }
    page:int{ label = "Level", bind = file:field(layout.level, "u8"), min = 1, max = 99 }
    page:dropdown{
        label = "Class", bind = file:field(layout.class, "u8"), options = { "Knight", "Mage", "Archer" },
        help = "Changing class resets mana",
        on_change = function(class) file:set(layout.mana, "u8", class == 1 and 80 or 20) end,
    }
    page:int{ label = "Mana", bind = file:field(layout.mana, "u8"), help = "Set by the class, can be changed afterwards" }
    page:toggle{ label = "Hard mode", bind = file:flag(layout.flags, 0) }
    page:page("Details", function(details)
        details:label{ label = "Checksum", value = file:u32(layout.checksum), format = "hex" }
        details:label{ label = "Save size", value = file:size() .. " bytes" }
        details:note("The checksum is recalculated when the save is written.")
    end)
end

local function inventory(page, file, items)
    local filter = page:state("bag-filter", "all")
    page:segmented{ label = "Show", bind = filter, options = { { value = "all", label = "All" }, { value = "used", label = "Used" } } }
    page:section({
        title = "Bag",
        actions = {
            { label = "Clear all", run = function() for i = 0, layout.bagSlots - 1 do file:set(layout.bag + i * 2, "u16", 0) end end },
            { label = "Fill with potions", run = function() for i = 0, layout.bagSlots - 1 do file:set(layout.bag + i * 2, "u16", 1) end end },
        },
    }, function(bag)
        for i = 0, layout.bagSlots - 1 do
            local slot = file:field(layout.bag + i * 2, "u16")
            if filter:get() == "all" or slot:get() ~= 0 then
                bag:dropdown{ key = "slot" .. i, label = "Slot " .. (i + 1), bind = slot, options = items }
            end
        end
    end)
end

local function world(page, file)
    page:tristate{ label = "Main quest", bind = file:field(layout.quest, "u8"), states = { "Locked", "Active", "Done" } }
    page:multiselect{
        label = "Elements", bind = file:field(layout.elements, "u8"),
        bits = { [0] = "Fire", [1] = "Ice", [2] = "Wind", [3] = "Earth" },
    }
    local dlc = file:flag(layout.flags, 1)
    page:toggle{ label = "Expansion unlocked", bind = dlc }
    if dlc:get() then
        page:int{ label = "Expansion level", bind = file:field(layout.dlcLevel, "u8"), max = 50 }
        page:dropdown{ label = "Expansion area", bind = file:field(layout.dlcArea, "u8"), options = { "Beach", "Cave", "Summit" } }
        page:toggle{ label = "Expansion boss beaten", bind = file:field(layout.dlcBoss, "u8"), on = 1, off = 0 }
    end
    page:popup({ label = "Danger zone" }, function(popup)
        popup:warning("These change several values at once. Use undo if you change your mind.")
        popup:button{ label = "Unlock everything", run = function()
            file:set(layout.quest, "u8", 2)
            file:set(layout.elements, "u8", 0x0F)
            file:setBit(layout.flags, 1, true)
            file:set(layout.dlcBoss, "u8", 1)
        end }
    end)
end

function build(ui, save)
    local file = save:file("save.dat")
    local items = st.labels("labels/items.json")
    ui:page("Player", function(page) player(page, file) end)
    ui:page("Inventory", function(page) inventory(page, file, items) end)
    ui:page("World", function(page) world(page, file) end)
end
