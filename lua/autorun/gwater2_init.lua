AddCSLuaFile()

gwater2 = nil

if SERVER then 
	include("gwater2_net.lua")
	include("gwater2_interactions.lua")

	AddCSLuaFile("gwater2_interactions.lua")
	AddCSLuaFile("gwater2_net.lua")
	AddCSLuaFile("gwater2_shaders.lua")
	AddCSLuaFile("gwater2_net.lua")
	return
end

local function error_message(text)
	local _, frame = pcall(function() return vgui.Create("DFrame") end)
	if not frame then
		timer.Simple(0, function()
			error_message(text)
		end)
		return
	end
	surface.PlaySound("gwater2/menu/toggle.wav")
	frame:SetTitle("GWater 2 Error")
	frame:SetDraggable(false)
	frame:ShowCloseButton(false)
	frame:SetBackgroundBlur(true)
	frame:SetSize(600, 200)

	local label = frame:Add("DLabel")
	label:SetText(text)
	label:SetContentAlignment(8)
	label:SetWrap(true)
	label:SetFont("Trebuchet16")
	label:SetTextColor(color_white)

	local button = frame:Add("DButton")
	button:SetText("OK")
	function button:DoClick()
		surface.PlaySound("gwater2/menu/select_deny.wav")
		frame:Close()
	end

	label:Dock(TOP)
	label:SetTall((#(label:GetText():Split("\n")))*16+8)
	button:Dock(TOP)

	frame:MakePopup()
	frame:SetTall(36 + label:GetTall() + button:GetTall())
	frame:Center()
	frame.label = label
	frame.button = button

	function frame:Paint(w, h)
		if self:GetBackgroundBlur() then
			Derma_DrawBackgroundBlur(self, self.m_fCreateTime)
		end

		surface.SetDrawColor(0, 0, 0, 190)
		surface.DrawRect(0, 0, w, h)

		surface.SetDrawColor(255, 255, 255)
		surface.DrawOutlinedRect(0, 0, w, h)

		surface.SetDrawColor(0, 0, 0, 190)
		surface.DrawRect(0, 0, w, 24)

		surface.SetDrawColor(255, 255, 255)
		surface.DrawOutlinedRect(0, 0, w, 24)
	end

	local washovered = false
	button:SetTextColor(Color(255, 255, 255))
	function button:Paint(w, h)
		if self:IsHovered() ~= washovered then
			washovered = self:IsHovered()
			if not self:IsHovered() then
				self:SetTextColor(Color(255, 255, 255))
			else
				surface.PlaySound("gwater2/menu/rollover.wav")
				self:SetTextColor(Color(0,  127, 255))
			end
		end
		surface.SetDrawColor(0, 0, 0, 190)
		surface.DrawRect(0, 0, w, h)

		surface.SetDrawColor(255, 255, 255)
		surface.DrawOutlinedRect(0, 0, w, h)
	end

	return frame
end

if system.IsLinux() or system.IsOSX() then
	error_message("GWater 2 is not supported!\n\n"..
				  "Gwater 2 is not supported on your system ("..(system.IsLinux() and "Linux" or "OSX")..")\n"..
				  "Please, use Proton instead.")
	return
end

local toload = (BRANCH == "x86-64" or BRANCH == "chromium" ) and "gwater2" or "gwater2_main" -- carrying

if not util.IsBinaryModuleInstalled(toload) then
	error_message("GWater 2 is not installed!\n\n"..
				  "GWater 2 failed to locate it's binary modules ("..toload..")\n"..
				  "Please make sure that you have installed GWater 2 correctly "..
				  "and that your branch and architecture is supported.\n"..
				  "BRANCH="..BRANCH.."    jit.arch="..jit.arch)
	return
end

-- load gmod_require if we can, just to get some useful information about failures
if file.Exists("lua/includes/modules/require.lua", "GAME") 
   and util.IsBinaryModuleInstalled("require.core") then
	print("[GWater 2] loading gmod_require")
	require("require")
end

local noerror, pcerr = pcall(function() require(toload) end)
if not noerror then
	pcerr = pcerr or "<no error message>"
	error_message("GWater 2 failed to load!\n\n"..
				  "This may happen because GWater could not find FleX binaries.\n"..
				  "Please check your install and restart the game.\n"..
				  "If you are sure that your install is correct and you still get that error,\n"..
				  "this can be a result of garry being a Very Unique Person With Weird Coding Styles :)\n"..
				  "The only solution is to restart your game until it works"..
				  " (can be any amount of restarts, usually 5-10)\n"..
				  "and report bug if issue persists.\n\n"..
				  "pcall error: "..pcerr.."\n"..
				  "BRANCH="..BRANCH.."\njit.arch="..jit.arch)
	return
end
local in_water = include("gwater2_interactions.lua")

-- GetMeshConvexes but for client
local function unfucked_get_mesh(ent, raw)
	-- Physics object exists
	local phys = ent:GetPhysicsObject()
	if phys:IsValid() then return (raw and phys:GetMesh() or phys:GetMeshConvexes()) end

	local model = ent:GetModel()
	local is_ragdoll = util.IsValidRagdoll(model)
	local convexes

	if !is_ragdoll or raw then
		local cs_ent = ents.CreateClientProp(model)
		local phys = cs_ent:GetPhysicsObject()
		convexes = phys:IsValid() and (raw and phys:GetMesh() or phys:GetMeshConvexes())
		cs_ent:Remove()
	else 
		-- no joke this is the hackiest shit ive ever done. 
		-- for whatever reason the metrocop and ONLY the metrocop model has this problem
		-- when creating a clientside ragdoll of the metrocop entity it will sometimes break all pistol and stunstick animations
		-- I have no idea why this happens.
		if model == "models/police.mdl" then model = "models/combine_soldier.mdl" end

		local cs_ent = ClientsideRagdoll(model)
		convexes = {}
		for i = 0, cs_ent:GetPhysicsObjectCount() - 1 do
			table.insert(convexes, cs_ent:GetPhysicsObjectNum(i):GetMesh())
		end
		cs_ent:Remove()
	end

	return convexes
end

-- adds entity to FlexSolver
local function add_prop(ent)
	if !IsValid(ent) then return end
	
	local ent_index = ent:EntIndex()
	gwater2.solver:RemoveCollider(ent_index) -- incase source decides to reuse the same entity index

	if !ent:IsSolid() or ent:IsWeapon() or !ent:GetModel() then return end

	local convexes = unfucked_get_mesh(ent)
	if !convexes then return end

	ent.GWATER2_IS_RAGDOLL = util.IsValidRagdoll(ent:GetModel())
	
	if #convexes < 16 then	-- too many convexes to be worth calculating
		for k, v in ipairs(convexes) do
			if #v <= 64 * 3 then	-- hardcoded limits.. No more than 64 planes per convex as it is a FleX limitation
				gwater2.solver:AddConvexCollider(ent_index, v, ent:GetPos(), ent:GetAngles())
			else
				gwater2.solver:AddConcaveCollider(ent_index, v, ent:GetPos(), ent:GetAngles())
			end
		end
	else
		gwater2.solver:AddConcaveCollider(ent_index, unfucked_get_mesh(ent, true), ent:GetPos(), ent:GetAngles())
	end

end

local function get_map_vertices()
	local all_vertices = {}
	for _, brush in ipairs(game.GetWorld():GetBrushSurfaces()) do
		local vertices = brush:GetVertices()
		for i = 3, #vertices do
			all_vertices[#all_vertices + 1] = vertices[1]
			all_vertices[#all_vertices + 1] = vertices[i - 1]
			all_vertices[#all_vertices + 1] = vertices[i]
		end
	end

	return all_vertices
end

-- collisions will lerp from positions they were at a long time ago if no particles have been initialized for a while
local no_lerp = false

gwater2 = {
	solver = FlexSolver(100000),
	renderer = FlexRenderer(),
	cloth_pos = Vector(),
	parameters = {},
	defaults = {},
	update_colliders = function(index, id, rep)
		if id == 0 then return end	-- skip, entity is world

		local ent = Entity(id)
		if !IsValid(ent) then 
			gwater2.solver:RemoveCollider(id)
		else 
			if !ent.GWATER2_IS_RAGDOLL then

				-- custom physics objects may be networked and initialized after the entity was created
				if ent.GWATER2_PHYSOBJ or ent:GetPhysicsObjectCount() != 0 then
					local phys = ent:GetPhysicsObject()	-- slightly expensive operation

					if !IsValid(ent.GWATER2_PHYSOBJ) or ent.GWATER2_PHYSOBJ != phys then	-- we know physics object was recreated with a PhysicsInit* function
						add_prop(ent)	-- internally cleans up entity colliders
						ent.GWATER2_PHYSOBJ = phys
					end
				end

				gwater2.solver:SetColliderPos(index, ent:GetPos(), no_lerp)
				gwater2.solver:SetColliderAng(index, ent:GetAngles(), no_lerp)
				gwater2.solver:SetColliderEnabled(index, ent:GetCollisionGroup() != COLLISION_GROUP_WORLD and bit.band(ent:GetSolidFlags(), FSOLID_NOT_SOLID) == 0)
			else
				-- horrible code for proper ragdoll collision. Still breaks half the time. Fuck source
				local bone_index = ent:TranslatePhysBoneToBone(rep)
				local pos, ang = ent:GetBonePosition(bone_index)
				if !pos or pos == ent:GetPos() then 	-- wtf?
					local bone = ent:GetBoneMatrix(bone_index)
					if bone then
						pos = bone:GetTranslation()
						ang = bone:GetAngles()
					else
						pos = ent:GetPos()
						ang = ent:GetAngles()
					end
				end
				gwater2.solver:SetColliderPos(index, pos, no_lerp)
				gwater2.solver:SetColliderAng(index, ang, no_lerp)
				
				if in_water(ent) then 
					gwater2.solver:SetColliderEnabled(index, false) 
					return 
				end

				local should_collide = ent:GetCollisionGroup() != COLLISION_GROUP_WORLD and bit.band(ent:GetSolidFlags(), FSOLID_NOT_SOLID) == 0
				if ent:IsPlayer() then
					gwater2.solver:SetColliderEnabled(index, should_collide and ent:GetNW2Bool("GWATER2_COLLISION", true))
				else
					gwater2.solver:SetColliderEnabled(index, should_collide)
				end
			end
		end
	end,

	reset_solver = function(err)
		xpcall(function()
			gwater2.solver:AddMapCollider(0, game.GetMap())
		end, function(e)
			gwater2.solver:AddConcaveCollider(0, get_map_vertices(), Vector(), Angle(0))
			if !err then
				ErrorNoHaltWithStack("[GWater2]: Map BSP structure is unsupported. Reverting to brushes. Collision WILL have holes!")
			end
		end)

		for k, ent in ipairs(ents.GetAll()) do
			add_prop(ent)
		end

		gwater2.solver:InitBounds(Vector(-16384, -16384, -16384), Vector(16384, 16384, 16384))	-- source bounds
	end,
	
	-- defined on server in gwater2_net.lua
	quick_matrix = function(pos, ang, scale)
		local mat = Matrix()
		if pos then mat:SetTranslation(pos) end
		if ang then mat:SetAngles(ang) end
		if scale then mat:SetScale(Vector(1, 1, 1) * scale) end
		return mat
	end
}

-- setup external default values
gwater2.parameters.color = Color(209, 237, 255, 25)
gwater2.parameters.color_value_multiplier = 1
gwater2.defaults = table.Copy(gwater2.parameters)

-- setup percentage values (used in menu)
gwater2["surface_tension"] = gwater2.solver:GetParameter("surface_tension") * gwater2.solver:GetParameter("radius")^4	-- dont ask me why its a power of 4
gwater2["fluid_rest_distance"] = gwater2.solver:GetParameter("fluid_rest_distance") / gwater2.solver:GetParameter("radius")
gwater2["solid_rest_distance"] = gwater2.solver:GetParameter("solid_rest_distance") / gwater2.solver:GetParameter("radius")
gwater2["collision_distance"] = gwater2.solver:GetParameter("collision_distance") / gwater2.solver:GetParameter("radius")
gwater2["cohesion"] = gwater2.solver:GetParameter("cohesion") * gwater2.solver:GetParameter("radius") * 0.1	-- cohesion scales by radius, for some reason..
gwater2["blur_passes"] = 3
-- reaction force specific
gwater2["force_multiplier"] = 0.01
gwater2["force_buoyancy"] = 0
gwater2["force_dampening"] = 0
gwater2["player_interaction"] = true
gwater2["touch_damage"] = 0

include("gwater2_shaders.lua")
include("gwater2_menu.lua")
include("gwater2_net.lua")

local limit_fps = 1 / 60
local function gwater_tick2()
	local lp = LocalPlayer()
	if !IsValid(lp) then return end

	if gwater2.solver:GetActiveParticles() <= 0 then 
		no_lerp = true
	else
		gwater2.solver:ApplyContacts(limit_fps * gwater2["force_multiplier"], 3, gwater2["force_buoyancy"], gwater2["force_dampening"])
		gwater2.solver:IterateColliders(gwater2.update_colliders)

		if no_lerp then 
			no_lerp = false
		end
	end
	
	local particles_in_radius = gwater2.solver:GetParticlesInRadius(lp:GetPos() + lp:OBBCenter(), gwater2.solver:GetParameter("fluid_rest_distance") * 3)
	GWATER2_QuickHackRemoveMeASAP(	-- TODO: REMOVE THIS HACKY SHIT!!!!!!!!!!!!!
		lp:EntIndex(), 
		particles_in_radius
	)

	lp.GWATER2_CONTACTS = particles_in_radius

	--[[
		for whatever reason if you drain particles before adding them it will cause 
		a problem where particles will swap velocities and positions randomly.

		im 5 hours in trying to figure out what flawed logic in my code is causing this to happen, and
		to be honest I do not have enough time or patience to figure out the underlying issue.. so for now we're
		gonna have to deal with some mee++
	]]
		
	-- TODO: rename to more gmod styled names, like `GWater2TickParticles`?
	pcall(function() hook.Run("gwater2_tick_particles") end)
	pcall(function() hook.Run("gwater2_tick_drains") end)

	gwater2.solver:Tick(limit_fps, 0)
end

timer.Create("gwater2_tick", limit_fps, 0, gwater_tick2)
hook.Add("InitPostEntity", "gwater2_addprop", gwater2.reset_solver)
hook.Add("OnEntityCreated", "gwater2_addprop", function(ent) timer.Simple(0, function() add_prop(ent) end) end)	// timer.0 so data values are setup correctly