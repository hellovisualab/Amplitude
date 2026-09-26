#pragma once

#include "CoreMinimal.h"

class UMaterialInstanceDynamic;
class UMaterialInterface;
class UStaticMesh;

/**
 * The handful of materials the 3D stage is built from. They are created in code so the project
 * needs no content: in the editor they are generated on first use (and can be saved with the
 * amp.SaveMaterials console command so packaged builds pick them up); anywhere else the engine's
 * basic shape material stands in.
 *
 * Every material exposes the same parameters: "Color" (vector), "Glow" (scalar emissive boost)
 * and "Roughness" (scalar, lit only). Vertex colours multiply the colour, so procedural meshes can
 * paint gradients while engine meshes (whose vertex colour is white) use the parameter alone.
 */
namespace AmplitudeMaterials
{
	enum class EKind : uint8
	{
		/** Default lit, two-sided. */
		Lit,
		/** Unlit emissive, two-sided (sky, glowing strips). */
		Unlit,
		/** Unlit additive translucency (sparks, beams, halos). */
		Additive,
		Count
	};

	UMaterialInterface* Get(EKind Kind);
	UMaterialInstanceDynamic* Create(EKind Kind, UObject* Outer, const FLinearColor& Color, float Glow = 0.0f, float Roughness = 0.4f);

	/** True when the generated materials are in use (false means the flat fallback material). */
	bool AreGenerated();

	/** Editor only: writes the generated materials to /Game/Amplitude/Materials. Returns how many were saved. */
	int32 SaveGenerated();

	UStaticMesh* GetSphere();
	UStaticMesh* GetCube();
	UStaticMesh* GetCylinder();
	UStaticMesh* GetCone();
}
