--if CLIENT then return end

local GWATER2_PARTICLES_TO_SWIM = 30

local function param_unfucked(name) -- dammit googer
	return gwater2.parameters[name]
end

-- swim code provided by kodya (with permission)
local gravity_convar = GetConVar("sv_gravity")
local function in_water(ply) 
	if param_unfucked("player_interaction") == false then return end
	if ply:OnGround() then return false end
	return ply.GWATER2_CONTACTS and ply.GWATER2_CONTACTS >= GWATER2_PARTICLES_TO_SWIM
end

hook.Add("CalcMainActivity", "gwater2_swimming", function(ply)
	if not in_water(ply) or ply:InVehicle() then return end
	return ACT_MP_SWIM, -1
end)

local function do_swim(ply, move)
	if not in_water(ply) then return end

	local vel = move:GetVelocity()
	local ang = move:GetMoveAngles()

	local acel =
	(ang:Forward() * move:GetForwardSpeed()) +
	(ang:Right() * move:GetSideSpeed()) +
	(ang:Up() * move:GetUpSpeed())

	local aceldir = acel:GetNormalized()
	local acelspeed = math.min(acel:Length(), ply:GetMaxSpeed())
	acel = aceldir * acelspeed * (param_unfucked("swimspeed") or 2)

	if bit.band(move:GetButtons(), IN_JUMP) ~= 0 then
		acel.z = acel.z + ply:GetMaxSpeed()
	end

	vel = vel + acel * FrameTime()
	vel = vel * (1 - FrameTime() * 2)

	local pgrav = ply:GetGravity() == 0 and 1 or ply:GetGravity()
	local gravity = pgrav * gravity_convar:GetFloat() * (param_unfucked("swimbuoyancy") or 0.49)
	vel.z = vel.z + FrameTime() * gravity

	move:SetVelocity(vel * (param_unfucked("swimfriction") or 1))
end

local function do_multiply(ply)
	if not ply.GWATER2_CONTACTS or ply.GWATER2_CONTACTS < (param_unfucked("multiplyparticles") or 60) then
		if not ply.GWATER2_MULTIPLIED then return end
		ply:SetWalkSpeed(ply.GWATER2_MULTIPLIED[1])
		ply:SetRunSpeed(ply.GWATER2_MULTIPLIED[2])
		ply:SetJumpPower(ply.GWATER2_MULTIPLIED[3])
		ply.GWATER2_MULTIPLIED = nil
		return
	end
	if ply.GWATER2_MULTIPLIED then return end
	ply.GWATER2_MULTIPLIED = {ply:GetWalkSpeed(), ply:GetRunSpeed(), ply:GetJumpPower()}
	ply:SetWalkSpeed(ply.GWATER2_MULTIPLIED[1] * (param_unfucked("multiplywalk") or 1))
	ply:SetRunSpeed(ply.GWATER2_MULTIPLIED[2] * (param_unfucked("multiplywalk") or 1))
	ply:SetJumpPower(ply.GWATER2_MULTIPLIED[3] * (param_unfucked("multiplyjump") or 1))
end

-- serverside ONLY
local function do_damage(ply)	
	if (gwater2.parameters.touchdamage or 0) == 0 then return end
	if not ply.GWATER2_CONTACTS or ply.GWATER2_CONTACTS < 30 then return end
	if gwater2.parameters.touchdamage > 0 then
		ply:TakeDamage(gwater2.parameters.touchdamage, Entity(1), Entity(1))
	else
		ply:SetHealth(math.min(ply:GetMaxHealth(), ply:Health() + -param_unfucked("touchdamage")))
	end
end

hook.Add("Move", "gwater2_swimming", function(ply, move)
	if param_unfucked("player_interaction") == false then return end

	do_swim(ply, move)
	do_multiply(ply)

	if SERVER then
		do_damage(ply)
	end
end)

hook.Add("FinishMove", "gwater2_swimming", function(ply, move)
	if not in_water(ply) then return end
	local vel = move:GetVelocity()
	local pgrav = ply:GetGravity() == 0 and 1 or ply:GetGravity()
	local gravity = pgrav * gravity_convar:GetFloat() * 0.5

	vel.z = vel.z + FrameTime() * gravity
	move:SetVelocity(vel)
end)

-- cancel fall damage when in water
hook.Add("GetFallDamage", "gwater2_swimming", function(ply, speed)
	if not ply.GWATER2_CONTACTS or ply.GWATER2_CONTACTS < GWATER2_PARTICLES_TO_SWIM then return end

	ply:EmitSound("Physics.WaterSplash")
	return 0
end)

return in_water