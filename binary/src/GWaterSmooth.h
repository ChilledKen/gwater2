#include <BaseVSShader.h>

#include "shaders/GWaterVolumetric_vs30.inc"
#include "shaders/GWaterSmooth_ps30.inc"

BEGIN_VS_SHADER(GWaterSmooth, "gwater2 helper")

// Shader parameters
BEGIN_SHADER_PARAMS
	SHADER_PARAM(RADIUS, SHADER_PARAM_TYPE_FLOAT, "1", "Radius of particles")
	SHADER_PARAM(SCRS, SHADER_PARAM_TYPE_VEC2, "[1 1]", "Screen Size")
	SHADER_PARAM(NORMALTEXTURE, SHADER_PARAM_TYPE_TEXTURE, "lights/white", "Texture of normals")
	SHADER_PARAM(DEPTHTEXTURE, SHADER_PARAM_TYPE_TEXTURE, "lights/white", "Texture of depth")
END_SHADER_PARAMS

SHADER_INIT_PARAMS() {

}

SHADER_INIT {
	LoadTexture(NORMALTEXTURE);
	LoadTexture(DEPTHTEXTURE);
}

SHADER_FALLBACK{
	return NULL;
}

SHADER_DRAW {
	SHADOW_STATE {

		// Note: Removing VERTEX_COLOR makes the shader work on all objects (Like props)
		unsigned int flags = VERTEX_POSITION | VERTEX_NORMAL | VERTEX_FORMAT_COMPRESSED;
		pShaderShadow->VertexShaderVertexFormat(flags, 1, 0, 0);
		pShaderShadow->EnableTexture(SHADER_SAMPLER0, true);
		pShaderShadow->EnableTexture(SHADER_SAMPLER1, true);

		DECLARE_STATIC_VERTEX_SHADER(GWaterNormals_vs30);
		SET_STATIC_VERTEX_SHADER(GWaterNormals_vs30);

		DECLARE_STATIC_PIXEL_SHADER(GWaterSmooth_ps30);
		SET_STATIC_PIXEL_SHADER(GWaterSmooth_ps30);
	}

	DYNAMIC_STATE {
		// constants
		const float* scr_s = params[SCRS]->GetVecValue();
		const float radius = params[RADIUS]->GetFloatValue();

		pShaderAPI->SetPixelShaderConstant(0, scr_s);
		pShaderAPI->SetPixelShaderConstant(1, &radius);

		BindTexture(SHADER_SAMPLER0, NORMALTEXTURE, FRAME);
		BindTexture(SHADER_SAMPLER1, DEPTHTEXTURE, FRAME);

		DECLARE_DYNAMIC_VERTEX_SHADER(GWaterVolumetric_vs30);
		SET_DYNAMIC_VERTEX_SHADER(GWaterVolumetric_vs30);

		DECLARE_DYNAMIC_PIXEL_SHADER(GWaterSmooth_ps30);
		SET_DYNAMIC_PIXEL_SHADER(GWaterSmooth_ps30);
	}

	Draw();

}

END_SHADER