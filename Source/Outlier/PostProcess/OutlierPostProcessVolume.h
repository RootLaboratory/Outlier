// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Engine/PostProcessVolume.h"
#include "OutlierPostProcessVolume.generated.h"

class UMaterialInterface;
class UMaterialParameterCollection;
class UCurveFloat;

UENUM(BlueprintType)
enum class EOutlierPostProcessMaterialType : uint8
{
	Scan UMETA(DisplayName = "Scan"),
	// Legacy. 은신은 메시 머티리얼 교체로 처리하므로 더 이상 포스트프로세스를 타지 않는다.
	// BP 의 PostProcessMaterials 맵 키 호환 때문에 값만 남겨 둔다 ( 엔트리는 지워도 된다 ).
	Stealth UMETA(DisplayName = "Stealth (Deprecated)"),
	Damaged UMETA(DisplayName = "Damaged"),
	// 자기장 발생기의 중력렌즈. BP 맵 키 호환 때문에 반드시 맨 뒤에 추가한다.
	Magnetic UMETA(DisplayName = "Magnetic"),
	// 파트너 아웃라인. 게임플레이 이벤트와 무관하게 상시 켜져 있는 유일한 패스다.
	PartnerOutline UMETA(DisplayName = "Partner Outline")

};

UCLASS(BlueprintType, Blueprintable)
class OUTLIER_API AOutlierPostProcessVolume : public APostProcessVolume
{
	GENERATED_BODY()

public:
	bool HasValidScanPostProcessBindings() const;
	bool HasValidPostProcessMaterial(EOutlierPostProcessMaterialType MaterialType) const;
	void SetPostProcessEnabled(EOutlierPostProcessMaterialType MaterialType, bool bInEnabled);
	
	void SetDamagedMaterialParameters(float InRatio);
	void SetScanMaterialParameters(FVector ScanLocation, float ScanRadius, float Range);
	void ResetPostProcessMaterialParameters();

	void UpdateScanMaterialParameters(FVector ScanLocation, float ScanRadius) const;

	// 자기장 렌즈. Scan 과 달리 원점 / 반경이 펄스 동안 고정이라 매 틱 갱신이 없다.
	// 진행도는 머티리얼이 Time 노드로 직접 계산하므로 여기서는 시작 / 종료 시각만 넘긴다.
	// InEndTime 을 현재 시각으로 넣으면 즉시 페이드아웃이 시작된다 ( 조기 중단 ).
	void SetMagneticMaterialParameters(FVector Origin, float Radius, float InStartTime, float InEndTime) const;
	// 원점 / 반경 / 시작 시각은 그대로 두고 종료 시각만 당긴다. 조기 중단용.
	void BeginMagneticFadeOut(float InEndTime) const;
	void ResetMagneticMaterialParameters() const;

	// 은신은 포스트프로세스가 아니라 메시 머티리얼 교체로 처리한다.
	// 이 볼륨은 어떤 머티리얼을 쓸지와 페이드 커브만 들고 있는 데이터 소스다.
	bool HasStealthMeshMaterials() const
	{
		return FirstPersonStealthGlassMaterial != nullptr || ThirdPersonStealthMaterial != nullptr;
	}
	UMaterialInterface* GetFirstPersonStealthGlassMaterial() const
	{
		return FirstPersonStealthGlassMaterial;
	}
	UMaterialInterface* GetThirdPersonStealthMaterial() const
	{
		return ThirdPersonStealthMaterial;
	}
	// StealthFadeCurve 를 적용한 0~1 페이드 값.
	float EvaluateStealthFade(float InLinearFade) const;
	void UpdateDamagedMaterialParameters(float InPlayerHPRatio) const;
	void UpdateDamagedMaterialParameters(float InPlayerHPRatio, FVector4 Color) const;
	void DisableAllBlendablesHard();

protected:
	virtual void BeginPlay() override;

private:
	bool SetBlendableWeight(EOutlierPostProcessMaterialType MaterialType, float Weight);
	void InitializeRuntimePostProcessMaterial(EOutlierPostProcessMaterialType MaterialType);

public:

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PostProcess", meta = (AllowPrivateAccess = "true"))
	TMap<EOutlierPostProcessMaterialType, TObjectPtr<UMaterialInterface>> PostProcessMaterials;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scan", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UMaterialParameterCollection> ScanParameterCollection;

	// 자기장 렌즈가 사라지는 데 걸리는 시간. 머티리얼의 FadeOut 상수와 같은 값이어야 한다.
	// 이 시간이 지난 뒤에 블렌더블 가중치를 0 으로 내려서 풀스크린 패스를 끊는다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Magnetic", meta = (ClampMin = "0.0", Units = "s"))
	float MagneticFadeOutDuration = 0.35f;

	// 교체한 머티리얼의 MID 에 넣어 줄 스칼라 파라미터 이름.
	// 해당 파라미터가 없는 머티리얼이면 그냥 무시된다 ( on/off 로만 동작 ).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stealth")
	FName StealthFadeParameterName = TEXT("Fade");

	// 은신 On/Off 시 Fade 가 0<->1 로 가는 데 걸리는 시간.
	// 0 이면 즉시 전환. UMaterialPostProcessSubsystem 이 이 값으로 보간한다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stealth", meta = (ClampMin = "0.0"))
	float StealthFadeDuration = 0.25f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stealth")
	TObjectPtr<UCurveFloat> StealthFadeCurve = nullptr;

	// 3인칭 메시( 남에게만 보이는 몸 / 무기 )에 덮어씌우는 표면 머티리얼.
	// 미지정이면 1인칭 글래스 머티리얼을 그대로 쓴다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stealth|Materials",
		meta = (DisplayName = "Third Person Stealth Material"))
	TObjectPtr<UMaterialInterface> ThirdPersonStealthMaterial = nullptr;

	// 1인칭 메시( 나에게만 보이는 팔 / 무기 )에 덮어씌우는 표면 머티리얼.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stealth|Materials",
		meta = (DisplayName = "First Person Stealth Glass Material"))
	TObjectPtr<UMaterialInterface> FirstPersonStealthGlassMaterial = nullptr;

private:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scan", meta = (AllowPrivateAccess = "true"))
	FName ScanRadiusParameterName = TEXT("Radius");

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scan", meta = (AllowPrivateAccess = "true"))
	FName ScanProgressParameterName = TEXT("Progress");

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scan", meta = (AllowPrivateAccess = "true"))
	FName ScanLocationParameterName = TEXT("Location");

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scan", meta = (AllowPrivateAccess = "true"))
	FName ScanFlagParameterName = TEXT("Flag");
	
	//플레이어에 따른 masking 색처리.

private:

	uint8 bScanPostProcessEnabled : 1 = false;
	uint8 bStealthPostProcessEnabled : 1 = false;
	uint8 bDamagedPostProcessEnabled : 1 = false;
	uint8 bMagneticPostProcessEnabled : 1 = false;
	uint8 bPartnerOutlinePostProcessEnabled : 1 = false;

	float ScanRangeRange = 0.f;
};
