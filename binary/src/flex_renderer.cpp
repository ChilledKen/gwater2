#include "flex_renderer.h"

//extern IMaterialSystem* materials = NULL;	// stops main branch compile from bitching

// Not sure why this isn't defined in the standard math library
#define min(a, b) a < b ? a : b

float u[3] = { 0.5 - SQRT3 / 2, 0.5, 0.5 + SQRT3 / 2 };
float v[3] = { 1, -0.5, 1 };

IMesh* build_mesh(int id, FlexRendererThreadData data) {
	int start = id * MAX_PRIMATIVES;
	int end = min((id + 1) * MAX_PRIMATIVES, data.max_particles);

	// We need to figure out how many and which particles are going to be rendered
	int particles_to_render = 0;
	for (int particle_index = start; particle_index < end; ++particle_index) {
		Vector particle_pos = data.particle_positions[particle_index].AsVector3D();

		// Frustrum culling
		Vector4D dst;
		Vector4DMultiply(data.view_projection_matrix, Vector4D(particle_pos.x, particle_pos.y, particle_pos.z, 1), dst);
		if (dst.z < 0 || -dst.x - dst.w > 0 || dst.x - dst.w > 0 || -dst.y - dst.w > 0 || dst.y - dst.w > 0) continue;
		
		// Add to our buffer
		data.render_buffer[start + particles_to_render] = particle_index;
		particles_to_render++;
	}

	// Don't even bother
	if (particles_to_render == 0) return nullptr;

	IMesh* mesh = materials->GetRenderContext()->CreateStaticMesh(VERTEX_POSITION | VERTEX_NORMAL | VERTEX_TEXCOORD0_2D, "");
	CMeshBuilder mesh_builder;
	mesh_builder.Begin(mesh, MATERIAL_TRIANGLES, particles_to_render);
	for (int i = start; i < start + particles_to_render; ++i) {
		int particle_index = data.render_buffer[i];
		Vector particle_pos = data.particle_positions[particle_index].AsVector3D();

		// calculate triangle rotation
		//Vector forward = (eye_pos - particle_pos).Normalized();
		Vector forward = (particle_pos - data.eye_pos).Normalized();
		Vector right = forward.Cross(Vector(0, 0, 1)).Normalized();
		Vector up = right.Cross(forward);
		Vector local_pos[3] = { (-up - right * SQRT3), up * 2.0, (-up + right * SQRT3) };


		if (data.particle_ani) {
			Vector4D ani1 = data.particle_ani0[particle_index];
			Vector4D ani2 = data.particle_ani1[particle_index];
			Vector4D ani3 = data.particle_ani2[particle_index];

			for (int i = 0; i < 3; i++) {
				// Anisotropy warping (code provided by Spanky)
				Vector pos_ani = local_pos[i];
				pos_ani = pos_ani + ani1.AsVector3D() * (local_pos[i].Dot(ani1.AsVector3D()) * ani1.w);
				pos_ani = pos_ani + ani2.AsVector3D() * (local_pos[i].Dot(ani2.AsVector3D()) * ani2.w);
				pos_ani = pos_ani + ani3.AsVector3D() * (local_pos[i].Dot(ani3.AsVector3D()) * ani3.w);

				Vector world_pos = particle_pos + pos_ani;
				mesh_builder.TexCoord2f(0, u[i], v[i]);
				mesh_builder.Position3f(world_pos.x, world_pos.y, world_pos.z);
				mesh_builder.Normal3f(-forward.x, -forward.y, -forward.z);
				mesh_builder.AdvanceVertex();
			}
		} else {
			for (int i = 0; i < 3; i++) { // Same as above w/o anisotropy warping
				Vector world_pos = particle_pos + local_pos[i] * data.radius;
				mesh_builder.TexCoord2f(0, u[i], v[i]);
				mesh_builder.Position3f(world_pos.x, world_pos.y, world_pos.z);
				mesh_builder.Normal3f(-forward.x, -forward.y, -forward.z);
				mesh_builder.AdvanceVertex();
			}
		}
	}
	mesh_builder.End();

	return mesh;
}

// Destroys all meshes related to water
void FlexRenderer::destroy_water() {
	IMatRenderContext* render_context = materials->GetRenderContext();
	for (int mesh = 0; mesh < allocated; mesh++) {
		if (water[mesh] == nullptr) continue;

		render_context->DestroyStaticMesh(water[mesh]);
		water[mesh] = nullptr;
	}
}

// lord have mercy brothers
void FlexRenderer::build_water(FlexSolver* flex, float radius) {
	// Clear previous imeshes since they are being rebuilt
	destroy_water();
	
	int max_particles = flex->get_active_particles();
	if (max_particles == 0) return;

	IMatRenderContext* render_context = materials->GetRenderContext();

	// View matrix, used in frustrum culling
	VMatrix view_matrix, projection_matrix, view_projection_matrix;
	render_context->GetMatrix(MATERIAL_VIEW, &view_matrix);
	render_context->GetMatrix(MATERIAL_PROJECTION, &projection_matrix);
	MatrixMultiply(projection_matrix, view_matrix, view_projection_matrix);
	
	// Get eye position for sprite calculations
	Vector eye_pos; render_context->GetWorldSpaceCameraPosition(&eye_pos);

	Vector4D* particle_positions = flex->get_parameter("smoothing") != 0 ? (Vector4D*)flex->get_host("particle_smooth") : (Vector4D*)flex->get_host("particle_pos");
	Vector4D* particle_ani0 = (Vector4D*)flex->get_host("particle_ani1");
	Vector4D* particle_ani1 = (Vector4D*)flex->get_host("particle_ani2");
	Vector4D* particle_ani2 = (Vector4D*)flex->get_host("particle_ani3");
	bool particle_ani = flex->get_parameter("anisotropy_scale") != 0;	// Should we do anisotropy calculations?

	// Update time!!!
	int max_meshes = min(ceil(max_particles / (float)MAX_PRIMATIVES), allocated);
	for (int mesh_index = 0; mesh_index < max_meshes; mesh_index++) {
		// update thread data
		FlexRendererThreadData data;
		data.eye_pos = eye_pos;
		data.view_projection_matrix = view_projection_matrix;
		data.particle_positions = particle_positions;
		data.max_particles = max_particles;
		data.radius = radius;
		data.particle_ani = particle_ani;
		data.particle_ani0 = particle_ani0;
		data.particle_ani1 = particle_ani1;
		data.particle_ani2 = particle_ani2;
		data.render_buffer = render_buffer;

		// Launch thread
		queue[mesh_index] = threads->enqueue(build_mesh, mesh_index, data);
	}

	//for (int mesh_index = 0; mesh_index < max_meshes; mesh_index++) {
	//	water[mesh_index] = queue.at(mesh_index).get();
	//}
};

void FlexRenderer::build_diffuse(FlexSolver* flex, float radius) {
	// IMPLEMENT ME!
};

void FlexRenderer::draw_diffuse() {
	// IMPLEMENT ME!
};

void FlexRenderer::update_water() {
	IMatRenderContext* render_context = materials->GetRenderContext();
	for (int mesh = 0; mesh < allocated; mesh++) {
		if (queue[mesh].valid()) {
			if (water[mesh] != nullptr) render_context->DestroyStaticMesh(water[mesh]);
			water[mesh] = queue[mesh].get();
		}
	}
}

void FlexRenderer::draw_water() {
	update_water();	// Update status of water meshes

	IMatRenderContext* render_context = materials->GetRenderContext();
	for (int mesh = 0; mesh < allocated; mesh++) {
		if (water[mesh] == nullptr) continue;

		water[mesh]->Draw();
	}
};

// Allocate buffers
FlexRenderer::FlexRenderer(int max_meshes) {
	allocated = max_meshes;

	water = (IMesh**)calloc(sizeof(IMesh*), allocated);
	if (!water) return;	// TODO: Fix undefined behavior if this statement runs

	threads = new ThreadPool(MAX_THREADS);

	queue = (std::future<IMesh*>*)calloc(sizeof(std::future<IMesh*>), allocated);	// Needs to be zero initialized
	if (!queue) return;	// out of ram?

	render_buffer = (int*)malloc(sizeof(int) * allocated * MAX_PRIMATIVES);
	if (!render_buffer) return;
};

FlexRenderer::~FlexRenderer() {
	if (water == nullptr) return;	// Never allocated (out of ram?)

	//if (materials->GetRenderContext() == nullptr) return;	// wtf?
	// Destroy existing meshes
	destroy_water();
	
	// Redestroy water that was being built in threads
	update_water();
	destroy_water();

	delete threads;

	free(render_buffer);
	free(water);
	free(queue);
};