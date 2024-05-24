// bsp parser requirements
// this MUST BE included before everything else because of conflicting source defines
#include "GMFS.h"
#include "BSPParser.h"

#include "GarrysMod/Lua/Interface.h"

#include "flex_solver.h"
#include "flex_renderer.h"
#include "shader_inject.h"

#define DRM

#ifdef DRM
#define CPPHTTPLIB_OPENSSL_SUPPORT
#include "httplib.h"
#include <fstream>
#include <iostream>
#include <string>
#endif

// Sys_LoadInterface symbols
/*#include "strtools_unicode.cpp"
#include "utlbuffer.cpp"
#include "utlstring.cpp"
#include "characterset.cpp"
#include "strtools.cpp"
#include "interface.cpp"*/

using namespace GarrysMod::Lua;

NvFlexLibrary* FLEX_LIBRARY;	// Main FleX library, handles all solvers. ("The heart of all that is FleX" - andreweathan)
ILuaBase* GLOBAL_LUA;			// used for flex error handling
int FLEXSOLVER_METATABLE = 0;
int FLEXRENDERER_METATABLE = 0;

//#define GET_FLEX(type, stack_pos) LUA->GetUserType<type>(stack_pos, type == FlexSolver ? FLEXSOLVER_METATABLE : FLEXRENDERER_METATABLE)

/************************** Flex Solver LUA Interface *******************************/

#define GET_FLEXSOLVER(stack_pos) LUA->GetUserType<FlexSolver>(stack_pos, FLEXSOLVER_METATABLE)

// Frees the flex solver instance from memory
LUA_FUNCTION(FLEXSOLVER_GarbageCollect) {
	LUA->CheckType(1, FLEXSOLVER_METATABLE);

	FlexSolver* flex = GET_FLEXSOLVER(1);

	LUA->PushNil();
	LUA->SetMetaTable(-2);

	delete flex;
	return 0;
}

LUA_FUNCTION(FLEXSOLVER_AddParticle) {
	LUA->CheckType(1, FLEXSOLVER_METATABLE);
	LUA->CheckType(2, Type::Vector);	// position
	LUA->CheckType(3, Type::Vector);	// velocity
	LUA->CheckNumber(4);				// mass

	FlexSolver* flex = GET_FLEXSOLVER(1);
	Vector pos = LUA->GetVector(2);
	Vector vel = LUA->GetVector(3);
	float inv_mass = 1.f / (float)LUA->GetNumber(4);	// FleX uses inverse mass for their calculations
	
	flex->add_particle(Vector4D(pos.x, pos.y, pos.z, inv_mass), vel);
	flex->map_particles();

	return 0;
}

LUA_FUNCTION(FLEXSOLVER_Tick) {
	LUA->CheckType(1, FLEXSOLVER_METATABLE);
	LUA->CheckNumber(2);	// Delta Time

	FlexSolver* flex = GET_FLEXSOLVER(1);
	
	// Avoid ticking if the deltatime ends up being zero, as it invalidates the simulation
	float dt = (float)LUA->GetNumber(2);
	if (flex->get_parameter("timescale") == 0 || dt == 0 || flex->get_active_particles() == 0) {
		LUA->PushBool(true);
		return 1;
	}

	bool succ = flex->pretick((NvFlexMapFlags)LUA->GetNumber(3));
	if (succ) flex->tick(dt);

	LUA->PushBool(succ);
	return 1;
}

// Adds a triangle collision mesh to a FlexSolver
LUA_FUNCTION(FLEXSOLVER_AddConcaveMesh) {
	LUA->CheckType(1, FLEXSOLVER_METATABLE);
	LUA->CheckNumber(2);				// Entity ID
	LUA->CheckType(3, Type::Table);		// Mesh data
	LUA->CheckType(4, Type::Vector);	// Initial Pos
	LUA->CheckType(5, Type::Angle);		// Initial Angle

	Vector pos = LUA->GetVector(4);
	QAngle ang = LUA->GetAngle(5);

	FlexSolver* flex = GET_FLEXSOLVER(1);
	std::vector<Vector> verts;	// mnnmm yess... vector vector
	for (int i = 1; i <= LUA->ObjLen(3); i++) {	// dont forget lua is 1 indexed!
		LUA->PushNumber(i);
		LUA->GetTable(3);
		LUA->GetField(-1, "pos");

		verts.push_back(LUA->GetType(-2) == Type::Vector ? LUA->GetVector(-2) : LUA->GetVector());
		LUA->Pop(2); //pop table & position
	}

	FlexMesh mesh = FlexMesh((int)LUA->GetNumber(2));
	if (!mesh.init_concave(FLEX_LIBRARY, verts, true)) {
		LUA->ThrowError("Tried to add concave mesh with invalid data (NumVertices is not a multiple of 3!)");
		return 0;
	}

	mesh.set_pos(pos);
	mesh.set_ang(ang);
	mesh.update();

	flex->add_mesh(mesh);

	return 0;
}

// Adds a convex collision mesh to a FlexSolver
LUA_FUNCTION(FLEXSOLVER_AddConvexMesh) {
	LUA->CheckType(1, FLEXSOLVER_METATABLE);
	LUA->CheckNumber(2);				// Entity ID
	LUA->CheckType(3, Type::Table);		// Mesh data
	LUA->CheckType(4, Type::Vector);	// Initial Pos
	LUA->CheckType(5, Type::Angle);		// Initial Angle

	Vector pos = LUA->GetVector(4);
	QAngle ang = LUA->GetAngle(5);

	FlexSolver* flex = GET_FLEXSOLVER(1);
	std::vector<Vector> verts;
	for (int i = 1; i <= LUA->ObjLen(3); i++) {	// dont forget lua is 1 indexed!
		LUA->PushNumber(i);
		LUA->GetTable(3);
		LUA->GetField(-1, "pos");

		verts.push_back(LUA->GetType(-2) == Type::Vector ? LUA->GetVector(-2) : LUA->GetVector());
		LUA->Pop(2); //pop table & position
	}

	FlexMesh mesh = FlexMesh((int)LUA->GetNumber(2));
	if (!mesh.init_convex(FLEX_LIBRARY, verts, true)) {
		LUA->ThrowError("Tried to add convex mesh with invalid data (NumVertices is not a multiple of 3!)");
		return 0;
	}

	mesh.set_pos(pos);
	mesh.set_ang(ang);
	mesh.update();

	flex->add_mesh(mesh);

	return 0;
}

// Removes all meshes associated with the entity id
LUA_FUNCTION(FLEXSOLVER_RemoveMesh) {
	LUA->CheckType(1, FLEXSOLVER_METATABLE);
	LUA->CheckNumber(2); // Entity ID

	FlexSolver* flex = GET_FLEXSOLVER(1);
	flex->remove_mesh(LUA->GetNumber(2));

	return 0;
}

// TODO: Implement
/*
LUA_FUNCTION(FLEXSOLVER_RemoveMeshIndex) {

}*/

LUA_FUNCTION(FLEXSOLVER_SetParameter) {
	LUA->CheckType(1, FLEXSOLVER_METATABLE);
	LUA->CheckString(2); // Param
	LUA->CheckNumber(3); // Number

	FlexSolver* flex = GET_FLEXSOLVER(1);
	bool succ = flex->set_parameter(LUA->GetString(2), LUA->GetNumber(3));
	if (!succ) LUA->ThrowError(("Attempt to set invalid parameter '" + (std::string)LUA->GetString(2) + "'").c_str());

	return 0;
}

LUA_FUNCTION(FLEXSOLVER_GetParameter) {
	LUA->CheckType(1, FLEXSOLVER_METATABLE);
	LUA->CheckString(2); // Param

	FlexSolver* flex = GET_FLEXSOLVER(1);
	float value = flex->get_parameter(LUA->GetString(2));
	if (isnan(value)) LUA->ThrowError(("Attempt to get invalid parameter '" + (std::string)LUA->GetString(2) + "'").c_str());
	LUA->PushNumber(value);

	return 1;
}

// Updates position and angles of a mesh collider
LUA_FUNCTION(FLEXSOLVER_UpdateMesh) {
	LUA->CheckType(1, FLEXSOLVER_METATABLE);
	LUA->CheckNumber(2);				// Mesh ID
	LUA->CheckType(3, Type::Vector);	// Prop Pos
	LUA->CheckType(4, Type::Angle);		// Prop Angle

	FlexSolver* flex = GET_FLEXSOLVER(1);
	flex->update_mesh(LUA->GetNumber(2), LUA->GetVector(3), LUA->GetAngle(4));

	return 0;
}

// removes all particles in a flex solver
LUA_FUNCTION(FLEXSOLVER_Reset) {
	LUA->CheckType(1, FLEXSOLVER_METATABLE);
	FlexSolver* flex = GET_FLEXSOLVER(1);
	flex->set_active_particles(0);

	return 0;
}

LUA_FUNCTION(FLEXSOLVER_GetActiveParticles) {
	LUA->CheckType(1, FLEXSOLVER_METATABLE);
	FlexSolver* flex = GET_FLEXSOLVER(1);
	LUA->PushNumber(flex->get_active_particles());

	return 1;
}

LUA_FUNCTION(FLEXSOLVER_GetActiveDiffuse) {
	LUA->CheckType(1, FLEXSOLVER_METATABLE);
	FlexSolver* flex = GET_FLEXSOLVER(1);
	LUA->PushNumber(flex->get_active_diffuse());

	return 1;
}


// Iterates through all particles and calls a lua function with 1 parameter (position)
LUA_FUNCTION(FLEXSOLVER_RenderParticles) {
	LUA->CheckType(1, FLEXSOLVER_METATABLE);
	LUA->CheckType(2, Type::Function);

	FlexSolver* flex = GET_FLEXSOLVER(1);
	Vector4D* host = (Vector4D*)flex->get_host("particle_pos");
	for (int i = 0; i < flex->get_active_particles(); i++) {
		// render function
		LUA->Push(2);
		LUA->PushVector(host[i].AsVector3D());
		LUA->PushNumber(host[i].w);
		LUA->Call(2, 0);
	}

	return 0;
}

// TODO: rewrite this shit
LUA_FUNCTION(FLEXSOLVER_AddMapMesh) {
	LUA->CheckType(1, FLEXSOLVER_METATABLE);
	LUA->CheckNumber(2);
	LUA->CheckString(3);	// Map name

	FlexSolver* flex = GET_FLEXSOLVER(1);

	// Get path, check if it exists
	std::string path = "maps/" + (std::string)LUA->GetString(3) + ".bsp";
	if (!FileSystem::Exists(path.c_str(), "GAME")) {
		LUA->ThrowError(("[GWater2 Internal Error]: Map path " + path + " not found! (Is the map subscribed to?)").c_str());
		return 0;
	}

	// Path exists, load data
	FileHandle_t file = FileSystem::Open(path.c_str(), "rb", "GAME");
	uint32_t filesize = FileSystem::Size(file);
	uint8_t* data = (uint8_t*)malloc(filesize);
	if (data == nullptr) {
		LUA->ThrowError("[GWater2 Internal Error]: Map collision data failed to load!");
		return 0;
	}
	FileSystem::Read(data, filesize, file);
	FileSystem::Close(file);

	BSPMap map = BSPMap(data, filesize, false);
	FlexMesh mesh = FlexMesh((int)LUA->GetNumber(2));
	if (!mesh.init_concave(FLEX_LIBRARY, (Vector*)map.GetVertices(), map.GetNumVertices(), false)) {
		free(data);
		LUA->ThrowError("Tried to add map mesh with invalid data (NumVertices is 0 or not a multiple of 3!)");

		return 0;
	}

	free(data);
	flex->add_mesh(mesh);

	return 0;
}

/*
* Returns a table of contact data. The table isnt sequential (do NOT use ipairs!) and in the format {[MeshIndex] = {ind1, cont1, pos1, vel1, ind2, cont2, pos2, vel2, ind3, cont3, pos3, vel3, etc...}
* Note that this function is quite expensive! It is a large table being generated.
* @param[in] solver The FlexSolver to return contact data for
* @param[in] buoyancymul A multiplier to the velocity of prop->water interation
*/
//FIXME: MOVE THIS FUNCTION TO THE FLEX_SOLVER CLASS
/*
LUA_FUNCTION(GetContacts) {
	LUA->CheckType(1, FlexMetaTable);
	LUA->CheckNumber(2);	// multiplier

	FlexSolver* flex = GET_FLEX;
	float4* pos = flex->get_host("particle_pos");
	float3* vel = (float3*)NvFlexMap(flex->getBuffer("particle_vel"), eNvFlexMapWait);
	float4* contact_vel = (float4*)NvFlexMap(flex->getBuffer("contact_vel"), eNvFlexMapWait);
	float4* planes = (float4*)NvFlexMap(flex->getBuffer("contact_planes"), eNvFlexMapWait);
	int* count = (int*)NvFlexMap(flex->getBuffer("contact_count"), eNvFlexMapWait);
	int* indices = (int*)NvFlexMap(flex->getBuffer("contact_indices"), eNvFlexMapWait);
	int max_contacts = flex->get_max_contacts();
	float radius = flex->get_parameter(LUA, "radius");
	float buoyancy_mul = LUA->GetNumber(2);
	std::map<int, Mesh*> props;

	// Get all props and average them
	for (int i = 0; i < flex->get_active_particles(); i++) {
		int particle = indices[i];
		for (int contact = 0; contact < count[particle]; contact++) {
		
			// Get force of impact (w/o friction) by multiplying plane direction by dot of hitnormal (incoming velocity)
			// incase particles are being pushed by it subtract velocity of mesh 
			int index = particle * max_contacts + contact;
			int prop_id = contact_vel[index].w;
			float3 plane = planes[index];
			float3 prop_pos = float3(pos[i]) - plane * radius;
			float3 impact_vel = plane * fmin(Dot(vel[i], plane), 0) - contact_vel[index] * buoyancy_mul;

			try {
				Mesh* prop = props.at(prop_id);
				prop->pos = prop->pos + float4(prop_pos.x, prop_pos.y, prop_pos.z, 1);
				prop->ang = prop->ang + float4(impact_vel.x, impact_vel.y, impact_vel.z, 0);
			}
			catch (std::exception e) {
				props[prop_id] = new Mesh(flexLibrary);
				props[prop_id]->pos = float4(prop_pos.x, prop_pos.y, prop_pos.z, 1);
				props[prop_id]->ang = float4(impact_vel.x, impact_vel.y, impact_vel.z, 0);
			}
		}
	}

	NvFlexUnmap(flex->getBuffer("particle_vel"));
	NvFlexUnmap(flex->getBuffer("contact_vel"));
	NvFlexUnmap(flex->getBuffer("contact_planes"));
	NvFlexUnmap(flex->getBuffer("contact_count"));
	NvFlexUnmap(flex->getBuffer("contact_indices"));
	
	// Average data together
	Vector pushvec = Vector();
	LUA->CreateTable();
	for (std::pair<int, Mesh*> prop : props) {
		int len = LUA->ObjLen();

		// Prop index
		LUA->PushNumber(len + 1);
		LUA->PushNumber(prop.first);
		LUA->SetTable(-3);

		// Number of particles contacting prop
		float average = prop.second->pos.w;
		LUA->PushNumber(len + 2);
		LUA->PushNumber(average);
		LUA->SetTable(-3);

		// Average position
		pushvec.x = prop.second->pos.x / average;
		pushvec.y = prop.second->pos.y / average;
		pushvec.z = prop.second->pos.z / average;
		LUA->PushNumber(len + 3);
		LUA->PushVector(pushvec);
		LUA->SetTable(-3);

		// Average velocity
		pushvec.x = prop.second->ang.x / average;
		pushvec.y = prop.second->ang.y / average;
		pushvec.z = prop.second->ang.z / average;
		LUA->PushNumber(len + 4);
		LUA->PushVector(pushvec);
		LUA->SetTable(-3);

		delete prop.second;
	}
	
	return 1;
}*/

// Original function written by andreweathan
LUA_FUNCTION(FLEXSOLVER_AddCube) {
	LUA->CheckType(1, FLEXSOLVER_METATABLE);
	LUA->CheckType(2, Type::Vector); // pos
	LUA->CheckType(3, Type::Vector); // vel
	LUA->CheckType(4, Type::Vector); // cube size
	LUA->CheckType(5, Type::Number); // size apart (usually radius)

	//gmod Vector and fleX float4
	FlexSolver* flex = GET_FLEXSOLVER(1);
	Vector gmodPos = LUA->GetVector(2);		//pos
	Vector gmodVel = LUA->GetVector(3);		//vel
	Vector gmodSize = LUA->GetVector(4);	//size
	float size = LUA->GetNumber(5);			//size apart

	gmodSize = gmodSize / 2.f;
	gmodPos = gmodPos + Vector(size, size, size) / 2.0;

	for (float z = -gmodSize.z; z < gmodSize.z; z++) {
		for (float y = -gmodSize.y; y < gmodSize.y; y++) {
			for (float x = -gmodSize.x; x < gmodSize.x; x++) {
				Vector newPos = Vector(x, y, z) * size + gmodPos;

				flex->add_particle(Vector4D(newPos.x, newPos.y, newPos.z, 1), gmodVel);
			}
		}
	}

	flex->map_particles();

	return 0;
}

// Initializes a box (6 planes) with a mins and maxs on a FlexSolver
// Inputting nil disables the bounds.
LUA_FUNCTION(FLEXSOLVER_InitBounds) {
	LUA->CheckType(1, FLEXSOLVER_METATABLE);
	FlexSolver* flex = GET_FLEXSOLVER(1);

	if (LUA->GetType(2) == Type::Vector && LUA->GetType(3) == Type::Vector) {
		flex->enable_bounds(LUA->GetVector(2), LUA->GetVector(3));
	} else {
		flex->disable_bounds();
	}
	
	return 0;
}

LUA_FUNCTION(FLEXSOLVER_GetMaxParticles) {
	LUA->CheckType(1, FLEXSOLVER_METATABLE);
	FlexSolver* flex = GET_FLEXSOLVER(1);

	LUA->PushNumber(flex->get_max_particles());
	return 1;
}

// Runs a lua function with some data on all FlexMeshes stored in a FlexSolver
// This is faster then returning a table of values and using ipairs and also allows removal / additions during function execution
// first parameter is the index of the mesh inside the vector
// second parameter is the entity id associated that was given during AddMesh
// third parameter is the number of reoccurring id's in a row (eg. given id's 0,1,1,1 the parameter would be 2 at the end of execution since 1 was repeated two more times)
// ^the third parameter sounds confusing but its useful for multi-joint entities such as ragdolls/players/npcs
LUA_FUNCTION(FLEXSOLVER_IterateMeshes) {
	LUA->CheckType(1, FLEXSOLVER_METATABLE);
	LUA->CheckType(2, Type::Function);
	FlexSolver* flex = GET_FLEXSOLVER(1);

	int i = 0;
	int repeat = 0;
	int previous_id;
	for (FlexMesh mesh : *flex->get_meshes()) {
		int id = mesh.get_mesh_id();

		repeat = (i != 0 && previous_id == id) ? repeat + 1 : 0;	// if (same as last time) {repeat = repeat + 1} else {repeat = 0}
		previous_id = id;

		// func(i, id, repeat)
		LUA->Push(2);
		LUA->PushNumber(i);
		LUA->PushNumber(id);
		LUA->PushNumber(repeat);
		LUA->PCall(3, 0, 0);

		i++;
	}

	return 0;
}


/*********************************** Flex Renderer LUA Interface *******************************************/

#define GET_FLEXRENDERER(stack_pos) LUA->GetUserType<FlexRenderer>(stack_pos, FLEXRENDERER_METATABLE)

// Frees the flex renderer and its allocated meshes from memory
LUA_FUNCTION(FLEXRENDERER_GarbageCollect) {
	LUA->CheckType(1, FLEXRENDERER_METATABLE);
	FlexRenderer* flex = GET_FLEXRENDERER(1);

	LUA->PushNil();
	LUA->SetMetaTable(-2);

	delete flex;
	return 0;
}

// FlexRenderer imesh related functions
LUA_FUNCTION(FLEXRENDERER_BuildWater) {
	LUA->CheckType(1, FLEXRENDERER_METATABLE);
	LUA->CheckType(2, FLEXSOLVER_METATABLE);
	LUA->CheckNumber(3);
	GET_FLEXRENDERER(1)->build_water(GET_FLEXSOLVER(2), LUA->GetNumber(3));

	return 0;
}

LUA_FUNCTION(FLEXRENDERER_DrawWater) {
	LUA->CheckType(1, FLEXRENDERER_METATABLE);
	GET_FLEXRENDERER(1)->draw_water();

	return 0;
}

LUA_FUNCTION(FLEXRENDERER_BuildDiffuse) {
	LUA->CheckType(1, FLEXRENDERER_METATABLE);
	LUA->CheckType(2, FLEXSOLVER_METATABLE);
	LUA->CheckNumber(3);
	GET_FLEXRENDERER(1)->build_diffuse(GET_FLEXSOLVER(2), LUA->GetNumber(3));

	return 0;
}

LUA_FUNCTION(FLEXRENDERER_DrawDiffuse) {
	LUA->CheckType(1, FLEXRENDERER_METATABLE);
	GET_FLEXRENDERER(1)->draw_diffuse();

	return 0;
}


/************************************* Global LUA Interface **********************************************/

#define ADD_FUNCTION(LUA, funcName, tblName) LUA->PushCFunction(funcName); LUA->SetField(-2, tblName)

// must be freed from memory
LUA_FUNCTION(NewFlexSolver) {
	LUA->CheckNumber(1);
	if (LUA->GetNumber(1) <= 0) LUA->ThrowError("Max Particles must be a positive number!");

	FlexSolver* flex = new FlexSolver(FLEX_LIBRARY, LUA->GetNumber(1));
	LUA->PushUserType(flex, FLEXSOLVER_METATABLE);
	LUA->PushMetaTable(FLEXSOLVER_METATABLE);	// Add our meta functions
	LUA->SetMetaTable(-2);

	/*
	NvFlexSolverCallback callback;
	callback.userData = LUA->GetUserdata();	// "DePriCatEd!!".. maybe add a function with identical or similar functionaliy... :/
	callback.function = [](NvFlexSolverCallbackParams p) {
		// hook.Call("GW2FlexCallback", nil, FlexSolver)
		GLOBAL_LUA->PushSpecial(SPECIAL_GLOB);
		GLOBAL_LUA->GetField(-1, "hook");
		GLOBAL_LUA->GetField(-1, "Call");
		GLOBAL_LUA->PushString("GW2FlexCallback");
		GLOBAL_LUA->PushNil();
		GLOBAL_LUA->PushUserdata(p.userData);	// ^
		GLOBAL_LUA->PushMetaTable(FLEXSOLVER_METATABLE);
		GLOBAL_LUA->SetMetaTable(-2);
		GLOBAL_LUA->PCall(3, 0, 0);
	};
	flex->add_callback(callback, eNvFlexStageUpdateEnd);*/

	return 1;
}

LUA_FUNCTION(NewFlexRenderer) {
	FlexRenderer* flex_renderer = new FlexRenderer();
	LUA->PushUserType(flex_renderer, FLEXRENDERER_METATABLE);
	LUA->PushMetaTable(FLEXRENDERER_METATABLE);
	LUA->SetMetaTable(-2);

	return 1;
}

#ifdef DRM

LUA_FUNCTION(TestDRM) {
	// gwater2 DRM (antipiracy)
	// Identification is done by reading the last 26 chars in the current binary, which are injected before download
	// We will call these chars a 'Key'
	// We send an HTTPS request to a server which returns if the key is considered valid
	// If the request fails in any way (returns anything other than 200) we instantly return
	// Keys can be invalidated if too many requsts (ips) are sent with the same key. If this is the case, the binary was likely distributed
	std::ifstream fs;
#if WIN64
	fs.open("garrysmod/lua/bin/gmcl_gwater2_win64.dll", std::ios::binary);	// Binary path is Local to GarrysMod/
#else
	fs.open("garrysmod/lua/bin/gmcl_gwater2_win32.dll", std::ios::binary);	//^
#endif
	if (fs.fail()) {
		Msg("Fuck!\n");
		// Fail code
	}
	else {
		// Read file, shove in std::string
		fs.seekg(0, std::ios::end);
		size_t i = fs.tellg();
		char* buf = new char[i];
		fs.seekg(0, std::ios::beg);
		fs.read(buf, i);
		fs.close();
		std::string s;
		s.assign(buf, i);
		delete[] buf;

		httplib::Client cli("https://gwater.misleadingname.cc");
		if (auto res = cli.Get("/verify.php" + s.substr(s.length() - 26, 26))) {
			Msg(std::to_string(res->status).c_str());
			Msg(res->body.c_str());
			// Success code
		}
		else {
			auto err = res.error();
			Msg(httplib::to_string(err).c_str());
			// Fail code
		}
	}

	return 0;
}
#endif
// `mat_antialias 0` but shit
/*LUA_FUNCTION(SetMSAAEnabled) {
	MaterialSystem_Config_t config = materials->GetCurrentConfigForVideoCard();
	config.m_nAAQuality = 0;
	config.m_nAASamples = 0;
	//config.m_nForceAnisotropicLevel = 1;
	//config.m_bShadowDepthTexture = false;
	//config.SetFlag(MATSYS_VIDCFG_FLAGS_FORCE_TRILINEAR, false);
	MaterialSystem_Config_t config_default = MaterialSystem_Config_t();
	//config.m_Flags = config_default.m_Flags;
	//config.m_DepthBias_ShadowMap = config_default.m_DepthBias_ShadowMap;
	//config.dxSupportLevel = config_default.dxSupportLevel;
	//config.m_SlopeScaleDepthBias_ShadowMap = config_default.m_SlopeScaleDepthBias_ShadowMap;
	materials->OverrideConfig(config, false);
	return 0;
}*/

GMOD_MODULE_OPEN() {
	GLOBAL_LUA = LUA;
	FLEX_LIBRARY = NvFlexInit(
		NV_FLEX_VERSION, 
		[](NvFlexErrorSeverity type, const char* message, const char* file, int line) {
			std::string error = "[GWater2 Internal Error]: " + (std::string)message;
			GLOBAL_LUA->ThrowError(error.c_str());
		}
	);

	if (FLEX_LIBRARY == nullptr) 
		LUA->ThrowError("[GWater2 Internal Error]: Nvidia FleX Failed to load! (Does your GPU meet the minimum requirements to run FleX?)");

	if (!Sys_LoadInterface("materialsystem", MATERIAL_SYSTEM_INTERFACE_VERSION, NULL, (void**)&materials))
		LUA->ThrowError("[GWater2 Internal Error]: C++ Materialsystem failed to load!");

	//if (!Sys_LoadInterface("shaderapidx9", SHADER_DEVICE_INTERFACE_VERSION, NULL, (void**)&g_pShaderDevice))
	//	LUA->ThrowError("[GWater2 Internal Error]: C++ Shaderdevice failed to load!");

	//if (!Sys_LoadInterface("shaderapidx9", SHADERAPI_INTERFACE_VERSION, NULL, (void**)&g_pShaderAPI))
	//	LUA->ThrowError("[GWater2 Internal Error]: C++ Shaderapi failed to load!");

	//if (!Sys_LoadInterface("studiorender", STUDIO_RENDER_INTERFACE_VERSION, NULL, (void**)&g_pStudioRender)) 
	//	LUA->ThrowError("[GWater2 Internal Error]: C++ Studiorender failed to load!");

	// Defined in 'shader_inject.h'
	if (!inject_shaders())
		LUA->ThrowError("[GWater2 Internal Error]: C++ Shadersystem failed to load!");

	// weird bsp filesystem
	if (FileSystem::LoadFileSystem() != FILESYSTEM_STATUS::OK)
		LUA->ThrowError("[GWater2 Internal Error]: C++ Filesystem failed to load!");
	
	FLEXSOLVER_METATABLE = LUA->CreateMetaTable("FlexSolver");
	ADD_FUNCTION(LUA, FLEXSOLVER_GarbageCollect, "__gc");	// FlexMetaTable.__gc = FlexGC

	// FlexMetaTable.__index = {func1, func2, ...}
	LUA->CreateTable();
	ADD_FUNCTION(LUA, FLEXSOLVER_GarbageCollect, "Destroy");
	ADD_FUNCTION(LUA, FLEXSOLVER_Tick, "Tick");
	ADD_FUNCTION(LUA, FLEXSOLVER_AddParticle, "AddParticle");
	ADD_FUNCTION(LUA, FLEXSOLVER_AddCube, "AddCube");
	ADD_FUNCTION(LUA, FLEXSOLVER_GetMaxParticles, "GetMaxParticles");
	ADD_FUNCTION(LUA, FLEXSOLVER_RenderParticles, "RenderParticles");
	ADD_FUNCTION(LUA, FLEXSOLVER_AddConcaveMesh, "AddConcaveMesh");
	ADD_FUNCTION(LUA, FLEXSOLVER_AddConvexMesh, "AddConvexMesh");
	ADD_FUNCTION(LUA, FLEXSOLVER_RemoveMesh, "RemoveMesh");
	ADD_FUNCTION(LUA, FLEXSOLVER_UpdateMesh, "UpdateMesh");
	ADD_FUNCTION(LUA, FLEXSOLVER_SetParameter, "SetParameter");
	ADD_FUNCTION(LUA, FLEXSOLVER_GetParameter, "GetParameter");
	ADD_FUNCTION(LUA, FLEXSOLVER_GetActiveParticles, "GetActiveParticles");
	ADD_FUNCTION(LUA, FLEXSOLVER_GetActiveDiffuse, "GetActiveDiffuse");
	ADD_FUNCTION(LUA, FLEXSOLVER_AddMapMesh, "AddMapMesh");
	ADD_FUNCTION(LUA, FLEXSOLVER_IterateMeshes, "IterateMeshes");
	//ADD_FUNCTION(LUA, GetContacts, "GetContacts");
	ADD_FUNCTION(LUA, FLEXSOLVER_InitBounds, "InitBounds");
	ADD_FUNCTION(LUA, FLEXSOLVER_Reset, "Reset");
	LUA->SetField(-2, "__index");

	FLEXRENDERER_METATABLE = LUA->CreateMetaTable("FlexRenderer");
	ADD_FUNCTION(LUA, FLEXRENDERER_GarbageCollect, "__gc");	// FlexMetaTable.__gc = FlexGC

	// FlexMetaTable.__index = {func1, func2, ...}
	LUA->CreateTable();
	ADD_FUNCTION(LUA, FLEXRENDERER_GarbageCollect, "Destroy");
	ADD_FUNCTION(LUA, FLEXRENDERER_BuildWater, "BuildWater");
	ADD_FUNCTION(LUA, FLEXRENDERER_DrawWater, "DrawWater");
	ADD_FUNCTION(LUA, FLEXRENDERER_BuildDiffuse, "BuildDiffuse");
	ADD_FUNCTION(LUA, FLEXRENDERER_DrawDiffuse, "DrawDiffuse");
	LUA->SetField(-2, "__index");

	// _G.FlexSolver = NewFlexSolver
	LUA->PushSpecial(SPECIAL_GLOB);
	ADD_FUNCTION(LUA, NewFlexSolver, "FlexSolver");
	ADD_FUNCTION(LUA, NewFlexRenderer, "FlexRenderer");
	ADD_FUNCTION(LUA, TestDRM, "TestDRM");
	LUA->Pop();

	return 0;
}

// Called when the module is unloaded
GMOD_MODULE_CLOSE() {
	NvFlexShutdown(FLEX_LIBRARY);
	FLEX_LIBRARY = nullptr;

	// Defined in 'shader_inject.h'
	eject_shaders();

	return 0;
}