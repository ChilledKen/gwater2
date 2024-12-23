AddCSLuaFile()

-- do return end

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

-- multi language support
local lang = GetConVar("cl_language"):GetString()
local strings = file.Read("data_static/gwater2/locale/gwater2_".. lang .. ".json", "THIRDPARTY")
if !strings then 
	lang = "english"
	strings = file.Read("data_static/gwater2/locale/gwater2_english.json", "THIRDPARTY") or "{}" 
end

for k,v in pairs(util.JSONToTable(strings)) do 
	language.Add(k, v) 
end

local function gw2_error(text)
	-- bigass logo to stand out
	text = [[   _____ __          __     _                ___  
  / ____|\ \        / /    | |              |__ \ 
 | |  __  \ \  /\  / /__ _ | |_  ___  _ __     ) |
 | | |_ |  \ \/  \/ // _` || __|/ _ \| '__|   / / 
 | |__| |   \  /\  /| (_| || |_|  __/| |     / /_ 
  \_____|    \/  \/  \__,_| \__|\___||_|    |____|
]]..text
	ErrorNoHalt(text) -- log to problem menu
	-- let's send our error later so that user has a higher chance of seeing it if it occured during loading
	timer.Simple(0, function()
		print("\n\n\n\n") -- a ton of spacing
		MsgC(Color(255, 0, 0), text, "\n") -- stand out in console even more
		print("\n\n\n\n") -- a ton of spacing
	end)
end

local toload = (BRANCH == "x86-64" or BRANCH == "chromium") and "gwater2" or "gwater2_main" -- carrying
if not util.IsBinaryModuleInstalled(toload) then
	gw2_error(string.format(
		"===========================================================\n\n" ..
		language.GetPhrase("gwater2.error.modulenotinstalled") .."\n\n" ..
		language.GetPhrase("gwater2.error.modulefailedtoload.3") .."\n\n" ..
		"===========================================================\n",
		"NONE", BRANCH, jit.arch
	))
	return
end

local noerror, pcerr = pcall(function() require(toload) end)
if not noerror then
	gw2_error(string.format(
		"===========================================================\n\n" ..
		language.GetPhrase("gwater2.error.modulefailedtoload.1").."\n"..
		language.GetPhrase("gwater2.error.modulefailedtoload.2").."\n\n"..
		language.GetPhrase("gwater2.error.modulefailedtoload.3") .."\n\n" ..
		"===========================================================\n",
		pcerr or "NONE", BRANCH, jit.arch
	))
	return
end

print("[GWater2]: Loaded successfully with language: " .. lang)
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
	error = error_message, -- in case we may want to use it
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

local function format_int(i)
	return tostring(i):reverse():gsub("%d%d%d", "%1,"):reverse():gsub("^,", "")
end

local show_time, hide_time, last_second_was, cur_second_parts, last_second = nil, nil, 0, 0, 0
hook.Add("HUDPaint", "gwater2_status", function()
	if CurTime() - last_second > 1 then
		last_second = CurTime()
		cur_second_parts = gwater2.solver:GetActiveParticles() - last_second_was
		last_second_was = gwater2.solver:GetActiveParticles()
	end
	local frac
	if gwater2.solver:GetActiveParticles() <= 0 then
		show_time = nil
		hide_time = hide_time or CurTime()
		frac = 1-math.ease.InCirc(math.min(1, CurTime()-hide_time))
	else
		show_time = show_time or CurTime()
		hide_time = nil
		frac = math.ease.OutCirc(math.min(1, CurTime()-show_time))
	end
	if gwater2.solver:GetActiveParticles() <= 0 and frac >= 1 then return end
	local text = format_int(gwater2.solver:GetActiveParticles()) .. " / " .. format_int(gwater2.solver:GetMaxParticles())
	text = text .. " ("..cur_second_parts..")"
	draw.DrawText(text, "CloseCaption_Normal", ScrW()/2+2, 18-18*(1-frac), Color(0, 0, 0, 255*frac), TEXT_ALIGN_CENTER)
	draw.DrawText(text, "CloseCaption_Normal", ScrW()/2, 16-18*(1-frac), ColorAlpha(color_white, 255*frac), TEXT_ALIGN_CENTER)
end)

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
-- water interaction specific
gwater2["force_multiplier"] = 0.01
gwater2["force_buoyancy"] = 0
gwater2["force_dampening"] = 0

gwater2["player_interaction"] = true

gwater2['swimspeed'] = 2
gwater2['swimbuoyancy'] = 0.49
gwater2['swimfriction'] = 1

gwater2["multiplyparticles"] = 60
gwater2["multiplywalk"] = 1
gwater2["multiplyjump"] = 1

gwater2["touchdamage"] = 0

include("gwater2_shaders.lua")
include("gwater2_menu.lua")
include("gwater2_net.lua")

local limit_fps = 1 / 60
local soundpatch

-- no need to calculate sound every frame
timer.Create("gwater2_calcdiffusesound", 0.1, 0, function()
	local lp = LocalPlayer()
	if !IsValid(lp) then return end

	soundpatch = soundpatch or CreateSound(lp, "gwater2/water_loop.wav")

	local percent = gwater2.solver:GetActiveDiffuseParticles() / gwater2.solver:GetMaxDiffuseParticles()
	if percent > 0.001 then
		local radius = (gwater2.solver:GetParameter("radius") / 10) ^ 0.75
		local sound_pos = gwater2.solver:GetActiveDiffuseParticlesPos(10)
		local dist = math.max(EyePos():DistToSqr(sound_pos) / 500000, 1)

		local volume = percent^0.6 / dist * radius	-- 0-1
		local pitch = math.Clamp(((200 - math.min(percent, 1 / 4) * 4 * 100) - dist * 5) / radius, 10, 250)	-- 10-250
		pitch = pitch - gwater2.solver:GetParameter("viscosity") * 5
		soundpatch:PlayEx(volume, pitch)
	else
		soundpatch:Stop()
	end

		
	local particles_in_radius = gwater2.solver:GetParticlesInRadius(lp:GetPos() + lp:OBBCenter(), gwater2.solver:GetParameter("fluid_rest_distance") * 3)

	-- multiplayer water-player interactions
	if lp:IsListenServerHost() then
		for _, ply in player.Iterator() do
			local particles = ply != lp and gwater2.solver:GetParticlesInRadius(ply:GetPos()+ ply:OBBCenter(), gwater2.solver:GetParameter("fluid_rest_distance") * 3)

			GWATER2_SET_CONTACTS(
				ply:EntIndex(), 
				particles or particles_in_radius
			)
		end
	end

	lp.GWATER2_CONTACTS = particles_in_radius
end)

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

	--[[
		for whatever reason if you drain particles before adding them it will cause 
		a problem where particles will swap velocities and positions randomly.

		im 5 hours in trying to figure out what flawed logic in my code is causing this to happen, and
		to be honest I do not have enough time or patience to figure out the underlying issue.. so for now we're
		gonna have to deal with some mee++
	]]
	local ok, err = pcall(function() hook.Run("gwater2_tick_particles") end)
	if not ok then ErrorNoHalt(err) end
	ok, err = pcall(function() hook.Run("gwater2_tick_drains") end)
	if not ok then ErrorNoHalt(err) end

	gwater2.solver:Tick(limit_fps, 0)
end

timer.Create("gwater2_tick", limit_fps, 0, gwater_tick2)
hook.Add("InitPostEntity", "gwater2_addprop", gwater2.reset_solver)
hook.Add("OnEntityCreated", "gwater2_addprop", function(ent) timer.Simple(0, function() add_prop(ent) end) end)	// timer.0 so data values are setup correctly