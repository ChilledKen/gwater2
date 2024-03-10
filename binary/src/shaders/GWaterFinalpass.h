#include <BaseVSShader.h>

#include "shaders/inc/GWaterFinalpass_vs30.inc"
#include "shaders/inc/GWaterFinalpass_ps30.inc"

BEGIN_VS_SHADER(GWaterFinalpass, "gwater2 helper")

// Shader parameters
BEGIN_SHADER_PARAMS
	SHADER_PARAM(RADIUS, SHADER_PARAM_TYPE_FLOAT, "1", "Radius of particles")
	SHADER_PARAM(NORMALTEXTURE, SHADER_PARAM_TYPE_TEXTURE, "lights/white", "Texture of smoothed normals")
	SHADER_PARAM(SCREENTEXTURE, SHADER_PARAM_TYPE_TEXTURE, "lights/white", "Texture of screen")
	SHADER_PARAM(DEPTHTEXTURE, SHADER_PARAM_TYPE_TEXTURE, "lights/white", "Depth texture")
	SHADER_PARAM(IOR, SHADER_PARAM_TYPE_FLOAT, "1.333", "Ior of water")
	SHADER_PARAM(COLOR2, SHADER_PARAM_TYPE_VEC4, "1.0 1.0 1.0 1.0", "Color of water. Alpha channel represents absorption amount")
	//SHADER_PARAM(ABSORPTIONMULTIPLIER, SHADER_PARAM_TYPE_FLOAT, "1", "Absorbsion multiplier")
	SHADER_PARAM(REFLECTANCE, SHADER_PARAM_TYPE_FLOAT, "0.5", "Reflectance of water")
	SHADER_PARAM(ENVMAP, SHADER_PARAM_TYPE_TEXTURE, "env_cubemap", "envmap")
END_SHADER_PARAMS

SHADER_INIT_PARAMS() {
	
}

SHADER_INIT {
	LoadCubeMap(ENVMAP, TEXTUREFLAGS_SRGB);
	LoadTexture(SCREENTEXTURE);
	LoadTexture(NORMALTEXTURE);
	LoadTexture(DEPTHTEXTURE);
}

SHADER_FALLBACK{
	return NULL;
}

SHADER_DRAW {
	SHADOW_STATE {
		// Note: Removing VERTEX_COLOR makes the shader work on all objects (Like props)
		unsigned int flags = VERTEX_POSITION | VERTEX_NORMAL | VERTEX_TEXCOORD0_2D;
		pShaderShadow->VertexShaderVertexFormat(flags, 1, 0, 0);
		pShaderShadow->EnableTexture(SHADER_SAMPLER0, true);	// Smoothed normals texture
		pShaderShadow->EnableTexture(SHADER_SAMPLER1, true);	// Screen texture
		pShaderShadow->EnableTexture(SHADER_SAMPLER2, true);	// Cubemap
		if (g_pHardwareConfig->GetHDRType() != HDR_TYPE_NONE) {
			pShaderShadow->EnableSRGBRead(SHADER_SAMPLER2, true);	// Doesn't seem to do anything?
		}
		pShaderShadow->EnableTexture(SHADER_SAMPLER3, true);	// Depth

		DECLARE_STATIC_VERTEX_SHADER(GWaterFinalpass_vs30);
		SET_STATIC_VERTEX_SHADER(GWaterFinalpass_vs30);

		DECLARE_STATIC_PIXEL_SHADER(GWaterFinalpass_ps30);
		SET_STATIC_PIXEL_SHADER(GWaterFinalpass_ps30);
	}

	DYNAMIC_STATE {
		// constants
		int scr_x, scr_y = 1; pShaderAPI->GetBackBufferDimensions(scr_x, scr_y);
		const float scr_s[2] = {scr_x, scr_y};
		float radius = params[RADIUS]->GetFloatValue();
		float ior = params[IOR]->GetFloatValue();
		float reflectance = params[REFLECTANCE]->GetFloatValue();
		const float* color2 = params[COLOR2]->GetVecValue();
		const float color2_normalized[4] = { color2[0] / 255.0, color2[1] / 255.0, color2[2] / 255.0, color2[3] / 255.0 };
		
		pShaderAPI->SetPixelShaderConstant(0, scr_s);
		pShaderAPI->SetPixelShaderConstant(1, &radius);
		pShaderAPI->SetPixelShaderConstant(2, &ior);
		pShaderAPI->SetPixelShaderConstant(3, &reflectance);
		pShaderAPI->SetPixelShaderConstant(4, color2_normalized);

		/*
		CMatRenderContextPtr pRenderContext(materials);

		// Yoinked from viewrender.cpp (in a water detection function of all things, ironic..)
		VMatrix viewMatrix, projectionMatrix, viewProjectionMatrix, inverseViewProjectionMatrix;
		pRenderContext->GetMatrix(MATERIAL_VIEW, &viewMatrix);
		pRenderContext->GetMatrix(MATERIAL_PROJECTION, &projectionMatrix);
		MatrixMultiply(projectionMatrix, viewMatrix, viewProjectionMatrix);
		MatrixInverseGeneral(viewProjectionMatrix, inverseViewProjectionMatrix);

		float matrix[16];
		for (int i = 0; i < 16; i++) {
			int x = i % 4;
			int y = i / 4;
			matrix[i] = inverseViewProjectionMatrix[y][x];
		}*/

		BindTexture(SHADER_SAMPLER0, NORMALTEXTURE);
		BindTexture(SHADER_SAMPLER1, SCREENTEXTURE);
		BindTexture(SHADER_SAMPLER2, ENVMAP);
		BindTexture(SHADER_SAMPLER3, DEPTHTEXTURE);
		
		DECLARE_DYNAMIC_VERTEX_SHADER(GWaterFinalpass_vs30);
		SET_DYNAMIC_VERTEX_SHADER(GWaterFinalpass_vs30);

		DECLARE_DYNAMIC_PIXEL_SHADER(GWaterFinalpass_ps30);
		SET_DYNAMIC_PIXEL_SHADER_COMBO(OPAQUE, color2[3] > 254);
		SET_DYNAMIC_PIXEL_SHADER(GWaterFinalpass_ps30);

		//pShaderAPI->SetVertexShaderConstant(4, matrix, 4, true);	// FORCE into cModelViewProj!
	}
	
	Draw();

}

END_SHADER