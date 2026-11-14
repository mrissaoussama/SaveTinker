-- Easy Save 3 files (Unity): optional gzip, optional AES-128-CBC with a key derived from a password.
local es3 = {}

local ITERATIONS = 100

local function key(password, iv)
    return st.crypto.pbkdf2("sha1", password, iv, ITERATIONS, 16)
end

function es3.encrypted(data)
    return data:match("^%s*[{%[]") == nil and #data >= 32 and #data % 16 == 0
end

-- Returns the JSON text, or nil and a reason. The default ES3 password is "password".
function es3.decode(data, password)
    local text = data
    if password and es3.encrypted(data) then
        local iv = data:sub(1, 16)
        local ok, plain = pcall(st.crypto.aes_decrypt, "cbc", key(password, iv), data:sub(17), iv)
        if not ok then return nil, plain end
        local pad = plain:byte(-1)
        if not pad or pad < 1 or pad > 16 or plain:sub(-pad) ~= string.rep(string.char(pad), pad) then
            return nil, "wrong password"
        end
        text = plain:sub(1, -pad - 1)
    end
    if text:sub(1, 2) == "\31\139" then
        local ok, unpacked = pcall(st.gzip.decompress, text)
        if not ok then return nil, unpacked end
        text = unpacked
    end
    return text
end

function es3.encode(text, password, options)
    local data = text
    if options and options.gzip then data = st.gzip.compress(data) end
    if not password then return data end
    local pad = 16 - #data % 16
    local bytes = {}
    for i = 1, 16 do bytes[i] = string.char(math.random(0, 255)) end
    local iv = table.concat(bytes)
    return iv .. st.crypto.aes_encrypt("cbc", key(password, iv), data .. string.rep(string.char(pad), pad), iv)
end

return es3
