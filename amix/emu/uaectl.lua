-- uaectl.lua - lets uaeamix.py drive WinUAE (the Windows build under Wine
-- or the Unix port). Load it with "lua=<this file>" in the config.
--
-- The host writes lines into $UAECTL_FILE (a temporary file, then renamed).
-- Each line is one uaectrl command - "floppy0 <path>", "KEY_RAW_DOWN 0x44",
-- "AKS_WARP 1", "dbg <debugger command>" - or "wait <frames>". They run in
-- order, one every other frame, so a key's press and release land in
-- different frames. When the queue is empty the hook writes
-- $UAECTL_FILE.done; command output goes to $UAECTL_FILE.out.
local cmdfile = os.getenv("UAECTL_FILE") or "uaectl.cmd"
local queue, head, tick, waitn = {}, 1, 0, 0

local function touch(name, text)
	local f = io.open(name, "w")
	if f then
		if text then f:write(text) end
		f:close()
	end
end

function on_uae_vsync()
	tick = tick + 1
	if tick % 2 ~= 0 then return end
	if head > #queue then
		local f = io.open(cmdfile, "r")
		if not f then return end
		queue, head = {}, 1
		for l in f:lines() do
			if l ~= "" then queue[#queue + 1] = l end
		end
		f:close()
		os.remove(cmdfile)
		os.remove(cmdfile .. ".done")
		touch(cmdfile .. ".out", "")
	end
	if waitn > 0 then
		waitn = waitn - 1
		return
	end
	local l = queue[head]
	if not l then return end
	head = head + 1
	local n = l:match("^wait%s+(%d+)$")
	if n then
		waitn = tonumber(n)
	else
		local r = uae_write_config(l)
		if r and r ~= "" then
			local f = io.open(cmdfile .. ".out", "a")
			if f then
				f:write(r, "\n")
				f:close()
			end
		end
	end
	if head > #queue then touch(cmdfile .. ".done") end
end
