#include "OutlierShieldMaterialCommandlet.h"

#include "FileHelpers.h"
#include "MaterialEditingLibrary.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionCustom.h"
#include "Materials/MaterialExpressionObjectLocalBounds.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionTransformPosition.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Materials/MaterialExpressionWorldPosition.h"

DEFINE_LOG_CATEGORY_STATIC(LogOutlierShieldMaterial, Log, All);

namespace OutlierShieldMaterial
{
	const TCHAR* MaterialPath =
		TEXT("/Game/Shield_FX_Responsive/Shield_Scifi_01/Material/M_Master_Shield_Octagon_01.M_Master_Shield_Octagon_01");
	const TCHAR* FillNodeDescription = TEXT("AdaptationColorFill");
	const FName BaseColorName(TEXT("Base_Color"));
	const FName GroupName(TEXT("Adaptation"));

	// 메시 로컬 바운드 기준 0(바닥)~1(꼭대기) 높이에서 Fill 아래는 현재 색, 위는 이전 색이다.
	// Fill 0과 1에서 경계 띠가 사라지도록 Softness만큼 범위를 늘려 정규화한다.
	const TCHAR* FillCode = TEXT(
		"float Height = max(BoundsMax.z - BoundsMin.z, 0.0001);\n"
		"float T = saturate((LocalPos.z - BoundsMin.z) / Height);\n"
		"float S = max(Softness, 0.0001);\n"
		"float Mask = saturate((Fill * (1.0 + S) - T) / S);\n"
		"float Band = Mask * (1.0 - Mask) * 4.0;\n"
		"return lerp(ColorPrevious, ColorCurrent, Mask) * (1.0 + Band * EdgeStrength);");

	template <typename TParam>
	TParam* CreateParameter(UMaterial* Material, FName Name, int32 X, int32 Y)
	{
		TParam* Param = Cast<TParam>(
			UMaterialEditingLibrary::CreateMaterialExpression(Material, TParam::StaticClass(), X, Y));
		Param->ParameterName = Name;
		Param->Group = GroupName;
		return Param;
	}

	void AddInput(UMaterialExpressionCustom* Custom, const TCHAR* Name, UMaterialExpression* From, int32 OutputIndex = 0)
	{
		FCustomInput& Input = Custom->Inputs.AddDefaulted_GetRef();
		Input.InputName = FName(Name);
		Input.Input.Connect(OutputIndex, From);
	}

	bool IsBaseColorParameter(const UMaterialExpression* Expression)
	{
		const UMaterialExpressionVectorParameter* Param = Cast<UMaterialExpressionVectorParameter>(Expression);
		return Param && Param->ParameterName == BaseColorName;
	}
}

UOutlierShieldMaterialCommandlet::UOutlierShieldMaterialCommandlet()
{
	IsClient = false;
	IsEditor = true;
	IsServer = false;
	LogToConsole = true;
}

int32 UOutlierShieldMaterialCommandlet::Main(const FString& Params)
{
	using namespace OutlierShieldMaterial;

	UMaterial* Material = LoadObject<UMaterial>(nullptr, MaterialPath);
	if (!Material)
	{
		UE_LOG(LogOutlierShieldMaterial, Error, TEXT("Material not found: %s"), MaterialPath);
		return 1;
	}

	const UMaterialExpressionVectorParameter* BaseColor = nullptr;
	for (UMaterialExpression* Expression : Material->GetExpressions())
	{
		const UMaterialExpressionCustom* Custom = Cast<UMaterialExpressionCustom>(Expression);
		if (Custom && Custom->Description == FillNodeDescription)
		{
			UE_LOG(LogOutlierShieldMaterial, Display, TEXT("Color fill already applied. Nothing to do."));
			return 0;
		}
		if (!BaseColor && IsBaseColorParameter(Expression))
		{
			BaseColor = Cast<UMaterialExpressionVectorParameter>(Expression);
		}
	}
	if (!BaseColor)
	{
		UE_LOG(LogOutlierShieldMaterial, Error, TEXT("Base_Color parameter not found."));
		return 1;
	}

	// 기존 Base_Color 근처(-32, -160) 왼쪽 위에 새 노드 묶음을 둔다.
	const int32 X = -1100;
	const int32 Y = -1000;

	UMaterialExpressionWorldPosition* WorldPosition = Cast<UMaterialExpressionWorldPosition>(
		UMaterialEditingLibrary::CreateMaterialExpression(Material, UMaterialExpressionWorldPosition::StaticClass(), X, Y));
	UMaterialExpressionTransformPosition* LocalPosition = Cast<UMaterialExpressionTransformPosition>(
		UMaterialEditingLibrary::CreateMaterialExpression(Material, UMaterialExpressionTransformPosition::StaticClass(), X + 220, Y));
	LocalPosition->TransformSourceType = TRANSFORMPOSSOURCE_World;
	LocalPosition->TransformType = TRANSFORMPOSSOURCE_Local;
	LocalPosition->Input.Connect(0, WorldPosition);

	UMaterialExpression* Bounds = UMaterialEditingLibrary::CreateMaterialExpression(
		Material, UMaterialExpressionObjectLocalBounds::StaticClass(), X + 220, Y + 100);

	UMaterialExpressionScalarParameter* Fill = CreateParameter<UMaterialExpressionScalarParameter>(
		Material, TEXT("Color_Fill"), X + 220, Y + 220);
	Fill->DefaultValue = 1.0f;
	Fill->SliderMin = 0.0f;
	Fill->SliderMax = 1.0f;

	UMaterialExpressionScalarParameter* Softness = CreateParameter<UMaterialExpressionScalarParameter>(
		Material, TEXT("Color_Fill_Softness"), X + 220, Y + 300);
	Softness->DefaultValue = 0.08f;

	UMaterialExpressionScalarParameter* EdgeStrength = CreateParameter<UMaterialExpressionScalarParameter>(
		Material, TEXT("Strength_Fill_Edge"), X + 220, Y + 380);
	EdgeStrength->DefaultValue = 0.0f;

	UMaterialExpressionVectorParameter* PreviousColor = CreateParameter<UMaterialExpressionVectorParameter>(
		Material, TEXT("Base_Color_Previous"), X + 220, Y + 460);
	PreviousColor->DefaultValue = BaseColor->DefaultValue;

	UMaterialExpressionCustom* Custom = Cast<UMaterialExpressionCustom>(
		UMaterialEditingLibrary::CreateMaterialExpression(Material, UMaterialExpressionCustom::StaticClass(), X + 560, Y + 150));
	Custom->Description = FillNodeDescription;
	Custom->Code = FillCode;
	Custom->OutputType = CMOT_Float3;
	Custom->Inputs.Reset();
	AddInput(Custom, TEXT("LocalPos"), LocalPosition);
	AddInput(Custom, TEXT("BoundsMin"), Bounds, 2);
	AddInput(Custom, TEXT("BoundsMax"), Bounds, 3);
	AddInput(Custom, TEXT("Fill"), Fill);
	AddInput(Custom, TEXT("Softness"), Softness);
	AddInput(Custom, TEXT("EdgeStrength"), EdgeStrength);
	AddInput(Custom, TEXT("ColorPrevious"), PreviousColor);
	AddInput(Custom, TEXT("ColorCurrent"), const_cast<UMaterialExpressionVectorParameter*>(BaseColor));

	// Base_Color를 직접 쓰던 곱셈들이 채움 결과를 쓰도록 바꾼다. Custom 자신의 입력은 제외한다.
	int32 RewiredCount = 0;
	for (UMaterialExpression* Expression : Material->GetExpressions())
	{
		if (!Expression || Expression == Custom)
		{
			continue;
		}
		for (FExpressionInputIterator It{ Expression }; It; ++It)
		{
			if (IsBaseColorParameter(It.Input->Expression))
			{
				It.Input->Connect(0, Custom);
				++RewiredCount;
			}
		}
	}

	UMaterialEditingLibrary::RecompileMaterial(Material);
	Material->MarkPackageDirty();
	if (!UEditorLoadingAndSavingUtils::SavePackages({ Material->GetOutermost() }, false))
	{
		UE_LOG(LogOutlierShieldMaterial, Error, TEXT("Failed to save %s"), MaterialPath);
		return 1;
	}

	UE_LOG(LogOutlierShieldMaterial, Display, TEXT("Color fill added. Rewired %d Base_Color input(s)."), RewiredCount);
	return 0;
}
