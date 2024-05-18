AddCSLuaFile()
local checkluatype = SF.CheckLuaType
local registerprivilege = SF.Permissions.registerPrivilege

--- Library for using gwater2
-- @name gwaterlib
-- @class library
-- @libtbl gwater_library
SF.RegisterLibrary("gwaterlib")

local function main(instance)
	local gwater_library = instance.Libraries.gwaterlib
	local vec_meta, vwrap, vunwrap = instance.Types.Vector, instance.Types.Vector.Wrap, instance.Types.Vector.Unwrap
	--- Spawns a GWater particle
	-- @client
	-- @param Vector pos
	-- @param Vector vel
	function gwater_library.addParticle(pos, vel)
		if LocalPlayer() == instance.player then
			gwater2.solver:AddParticle(vunwrap(pos), vunwrap(vel), 1, 1)
		end
	end

	--- Spawns a GWater particle cube
	-- @client
	-- @param Vector pos
	-- @param Vector vel
	-- @param Vector size
	-- @param number apart
	function gwater_library.addCube(pos, vel, size, apart)
		if LocalPlayer() == instance.player then
			gwater2.solver:AddCube(vunwrap(pos), vunwrap(vel), vunwrap(size), apart)
		end
	end

	--- Clears All Gwater
	-- @client
	function gwater_library.clearAllParticles()
		if LocalPlayer() == instance.player then
			gwater2.solver:Reset()
		end
	end

	--- Changes the chosen Gwater parameter.
	-- @client
	-- @param string parameter
	-- @param number value
	function gwater_library.setParameter(parameter, value)
		if LocalPlayer() == instance.player then
			SetGwaterParameter(parameter, value)
		end
	end

	--- Gets the chosen Gwater parameter.
	-- @client
	-- @param string parameter
	-- @return number value
	function gwater_library.getParameter(parameter)
		if LocalPlayer() == instance.player then
			return gwater2.solver:GetParameter(parameter)
		end
	end
end

return main
