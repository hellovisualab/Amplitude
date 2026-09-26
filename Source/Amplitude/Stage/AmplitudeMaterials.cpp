#include "Stage/AmplitudeMaterials.h"

#include "Amplitude.h"
#include "Engine/StaticMesh.h"
#include "HAL/IConsoleManager.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"

#if WITH_EDITOR
#include "Materials/MaterialExpressionMultiply.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Materials/MaterialExpressionVertexColor.h"
#include "UObject/SavePackage.h"
#endif

namespace
{
	using AmplitudeMaterials::EKind;

	constexpr int32 KindCount = static_cast<int32>(EKind::Count);
	const TCHAR* const MaterialFolder = TEXT("/Game/Amplitude/Materials");
	const TCHAR* const MaterialNames[KindCount] = {TEXT("M_AmpLit"), TEXT("M_AmpUnlit"), TEXT("M_AmpAdditive")};
	const TCHAR* const FallbackMaterialPath = TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial");

	UMaterialInterface* Cache[KindCount] = {};
	bool bCacheIsGenerated = false;

	FString GetPackagePath(EKind Kind)
	{
		return FString::Printf(TEXT("%s/%s"), MaterialFolder, MaterialNames[static_cast<int32>(Kind)]);
	}

	/** A material saved by amp.SaveMaterials (or authored by hand) takes precedence. */
	UMaterialInterface* LoadSaved(EKind Kind)
	{
		const FString PackagePath = GetPackagePath(Kind);
		const FString ObjectPath = FString::Printf(TEXT("%s.%s"), *PackagePath, MaterialNames[static_cast<int32>(Kind)]);
		if (UMaterialInterface* Loaded = FindObject<UMaterialInterface>(nullptr, *ObjectPath))
		{
			return Loaded;
		}
		if (!FPackageName::DoesPackageExist(PackagePath))
		{
			return nullptr;
		}
		return LoadObject<UMaterialInterface>(nullptr, *ObjectPath, nullptr, LOAD_NoWarn | LOAD_Quiet);
	}

#if WITH_EDITOR
	template <typename TExpression>
	TExpression* AddExpression(UMaterial* Material)
	{
		TExpression* Expression = NewObject<TExpression>(Material);
		Expression->Material = Material;
		Material->GetExpressionCollection().AddExpression(Expression);
		return Expression;
	}

	/** Color x VertexColor feeds Base Color (lit) and, scaled by Glow, Emissive. */
	UMaterial* BuildMaterial(EKind Kind)
	{
		const FString Name = MaterialNames[static_cast<int32>(Kind)];
		UPackage* Package = CreatePackage(*GetPackagePath(Kind));
		UMaterial* Material = NewObject<UMaterial>(Package, FName(*Name), RF_Public | RF_Standalone);

		UMaterialExpressionVectorParameter* Color = AddExpression<UMaterialExpressionVectorParameter>(Material);
		Color->ParameterName = TEXT("Color");
		Color->DefaultValue = FLinearColor::White;

		UMaterialExpressionScalarParameter* Glow = AddExpression<UMaterialExpressionScalarParameter>(Material);
		Glow->ParameterName = TEXT("Glow");
		Glow->DefaultValue = Kind == EKind::Lit ? 0.0f : 1.0f;

		UMaterialExpressionVertexColor* VertexColor = AddExpression<UMaterialExpressionVertexColor>(Material);

		UMaterialExpressionMultiply* Tinted = AddExpression<UMaterialExpressionMultiply>(Material);
		Tinted->A.Connect(0, Color);
		Tinted->B.Connect(0, VertexColor);

		UMaterialExpressionMultiply* Emissive = AddExpression<UMaterialExpressionMultiply>(Material);
		Emissive->A.Connect(0, Tinted);
		Emissive->B.Connect(0, Glow);

		UMaterialEditorOnlyData* EditorData = Material->GetEditorOnlyData();
		EditorData->EmissiveColor.Connect(0, Emissive);

		if (Kind == EKind::Lit)
		{
			UMaterialExpressionScalarParameter* Roughness = AddExpression<UMaterialExpressionScalarParameter>(Material);
			Roughness->ParameterName = TEXT("Roughness");
			Roughness->DefaultValue = 0.4f;
			EditorData->BaseColor.Connect(0, Tinted);
			EditorData->Roughness.Connect(0, Roughness);
			Material->SetShadingModel(MSM_DefaultLit);
		}
		else
		{
			Material->SetShadingModel(MSM_Unlit);
			Material->BlendMode = Kind == EKind::Additive ? BLEND_Additive : BLEND_Opaque;
		}

		Material->TwoSided = true;
		Material->bUsedWithInstancedStaticMeshes = true;
		Material->PreEditChange(nullptr);
		Material->PostEditChange();
		Material->MarkPackageDirty();
		return Material;
	}
#endif

	void EnsureCache()
	{
		if (Cache[0] != nullptr)
		{
			return;
		}

		bool bAllFound = true;
		for (int32 Index = 0; Index < KindCount; ++Index)
		{
			Cache[Index] = LoadSaved(static_cast<EKind>(Index));
			bAllFound &= Cache[Index] != nullptr;
		}
		bCacheIsGenerated = bAllFound;

#if WITH_EDITOR
		if (!bAllFound)
		{
			for (int32 Index = 0; Index < KindCount; ++Index)
			{
				if (Cache[Index] == nullptr)
				{
					Cache[Index] = BuildMaterial(static_cast<EKind>(Index));
				}
			}
			bCacheIsGenerated = true;
			UE_LOG(LogAmplitude, Log, TEXT("Generated the stage materials in memory (run amp.SaveMaterials to keep them for packaged builds)."));
		}
#endif

		if (!bCacheIsGenerated)
		{
			UMaterialInterface* Fallback = LoadObject<UMaterialInterface>(nullptr, FallbackMaterialPath);
			if (Fallback == nullptr)
			{
				Fallback = UMaterial::GetDefaultMaterial(MD_Surface);
			}
			UE_LOG(LogAmplitude, Warning, TEXT("Stage materials not found in %s; using the engine's basic shape material."), MaterialFolder);
			for (int32 Index = 0; Index < KindCount; ++Index)
			{
				Cache[Index] = Fallback;
			}
		}

		for (UMaterialInterface* Material : Cache)
		{
			if (Material != nullptr && !Material->IsRooted())
			{
				Material->AddToRoot();
			}
		}
	}

	UStaticMesh* LoadEngineMesh(const TCHAR* Path)
	{
		UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, Path);
		if (Mesh == nullptr)
		{
			UE_LOG(LogAmplitude, Error, TEXT("Engine mesh %s is missing"), Path);
		}
		return Mesh;
	}

	FAutoConsoleCommand SaveMaterialsCommand(
		TEXT("amp.SaveMaterials"),
		TEXT("Editor only: saves the generated stage materials to /Game/Amplitude/Materials so packaged builds can use them."),
		FConsoleCommandDelegate::CreateLambda([]()
		{
			const int32 Saved = AmplitudeMaterials::SaveGenerated();
			UE_LOG(LogAmplitude, Display, TEXT("amp.SaveMaterials: saved %d material(s)."), Saved);
		}));
}

namespace AmplitudeMaterials
{
	UMaterialInterface* Get(EKind Kind)
	{
		EnsureCache();
		return Cache[FMath::Clamp(static_cast<int32>(Kind), 0, KindCount - 1)];
	}

	UMaterialInstanceDynamic* Create(EKind Kind, UObject* Outer, const FLinearColor& Color, float Glow, float Roughness)
	{
		UMaterialInstanceDynamic* Instance = UMaterialInstanceDynamic::Create(Get(Kind), Outer);
		if (Instance != nullptr)
		{
			Instance->SetVectorParameterValue(TEXT("Color"), Color);
			Instance->SetScalarParameterValue(TEXT("Glow"), Glow);
			Instance->SetScalarParameterValue(TEXT("Roughness"), Roughness);
		}
		return Instance;
	}

	bool AreGenerated()
	{
		EnsureCache();
		return bCacheIsGenerated;
	}

	int32 SaveGenerated()
	{
		int32 Saved = 0;
#if WITH_EDITOR
		EnsureCache();
		for (int32 Index = 0; Index < KindCount; ++Index)
		{
			UMaterial* Material = Cast<UMaterial>(Cache[Index]);
			UPackage* Package = Material != nullptr ? Material->GetPackage() : nullptr;
			if (Package == nullptr || !Package->GetName().StartsWith(MaterialFolder))
			{
				continue;
			}
			const FString FileName = FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension());
			FSavePackageArgs SaveArgs;
			SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
			if (UPackage::SavePackage(Package, Material, *FileName, SaveArgs))
			{
				++Saved;
			}
			else
			{
				UE_LOG(LogAmplitude, Warning, TEXT("Could not save %s"), *FileName);
			}
		}
#endif
		return Saved;
	}

	UStaticMesh* GetSphere()
	{
		// Not cached: engine content can be garbage collected between play sessions in the editor.
		return LoadEngineMesh(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	}

	UStaticMesh* GetCube()
	{
		// Not cached: engine content can be garbage collected between play sessions in the editor.
		return LoadEngineMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
	}

	UStaticMesh* GetCylinder()
	{
		// Not cached: engine content can be garbage collected between play sessions in the editor.
		return LoadEngineMesh(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	}

	UStaticMesh* GetCone()
	{
		// Not cached: engine content can be garbage collected between play sessions in the editor.
		return LoadEngineMesh(TEXT("/Engine/BasicShapes/Cone.Cone"));
	}
}
