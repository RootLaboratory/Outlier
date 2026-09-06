// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Subsystems/WorldSubsystem.h"
#include "MaterialPostProcessSubsystem.generated.h"

class AOutlierPostProcessVolume;
class UMaterialInterface;
class UMeshComponent;
class UOutlierAbilitySystemComponent;
class UPrimitiveComponent;
enum class EOutlierPostProcessMaterialType : uint8;

struct FScanStencilRestoreState
{
	bool bRenderCustomDepth = false;
	int32 CustomDepthStencilValue = 0;
};

// 은신 오버라이드를 걸기 전 메시 컴포넌트의 원본 상태.
// Materials 는 1인칭 글래스로 교체한 메시에만 채워진다 ( 3인칭은 스텐실만 건드리므로 빈 배열 ).
USTRUCT()
struct FOutlierStealthMeshRestoreState
{
	GENERATED_BODY()

	UPROPERTY()
	bool bRenderCustomDepth = false;

	UPROPERTY()
	int32 CustomDepthStencilValue = 0;

	UPROPERTY()
	TArray<TObjectPtr<UMaterialInterface>> Materials;
};

// State.Stealthed 를 들 수 있는 ASC 1개분 상태.
// 아바타 액터는 도중에 바뀔 수 있으므로 캐시하지 않고 ASC 에서 매번 다시 얻는다.
struct FOutlierStealthSourceState
{
	FDelegateHandle TagChangedHandle;
	float CurrentFade = 0.0f;
	float TargetFade = 0.0f;
	bool bStealthTagActive = false;
};

UCLASS()
class OUTLIER_API UMaterialPostProcessSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

	void RegisterPostProcessVolume(AOutlierPostProcessVolume* InPostProcessVolume);
	void SetPostProcessEnabled(EOutlierPostProcessMaterialType MaterialType, bool bEnabled);
	void Refresh();
	void DisableAllBoundPostProcessMaterials();
	void FlushScanStencilRestoreStates();
	void FlushPostProcessMaterialParameters();

	//Scan
	void StartScanPostProcess(FVector ScanOrigin, float CurrentScanRadius, float Range);
	void UpdateScanPostProcess(FVector ScanOrigin, float CurrentScanRadius);
	void ApplyScanStencil(AActor* Actor, int32 StencilValue);
	void ClearScanStencil(AActor* Actor);
	void EndScanPostProcess();

	// Stealth
	// 은신은 State.Stealthed 태그를 구독해서 이 서브시스템이 전담한다.
	// 캐릭터/무기는 IOutlierStealthVisualTarget 으로 자기 메시 구성만 답하고,
	// 적용 대상 집합( 무기 교체 / 파트너 교체 )은 매 틱 재수집해서 자동으로 따라간다.
	void RegisterStealthSource(UOutlierAbilitySystemComponent* AbilitySystem);
	void UnregisterStealthSource(UOutlierAbilitySystemComponent* AbilitySystem);
	void FlushStealthRestoreStates();
	UMaterialInterface* GetFirstPersonStealthGlassMaterial() const;

	//Damaged
	void UpdateDamagedPostProcess(float InHPRatio);
	void UpdateDamagedPostProcess(float InHPRatio, FVector4 Color);
	void EndDamagedPostProcess();

	UPROPERTY()
	TObjectPtr<AOutlierPostProcessVolume> BoundPostProcessVolume;

private:
	TMap<TWeakObjectPtr<UPrimitiveComponent>, FScanStencilRestoreState> ScanStencilRestoreStates;

	// 오버라이드가 걸려 있는 메시와 그 원본. 원본 머티리얼을 GC 로부터 지켜야 하므로 UPROPERTY.
	UPROPERTY()
	TMap<TObjectPtr<UMeshComponent>, FOutlierStealthMeshRestoreState> StealthMeshRestoreStates;

	TMap<TWeakObjectPtr<UOutlierAbilitySystemComponent>, FOutlierStealthSourceState> StealthSources;

	// 매 틱 재사용하는 스크래치 ( 할당 방지 ).
	TArray<UMeshComponent*> ScratchFirstPersonMeshes;
	TArray<UMeshComponent*> ScratchThirdPersonMeshes;
	TArray<TObjectPtr<UMeshComponent>> ScratchStaleMeshes;

	bool ShouldSkipRenderingWork() const;
	void ClearAllScanStencils();

	void HandleStealthTagChanged(
		const FGameplayTag Tag,
		int32 NewCount,
		TWeakObjectPtr<UOutlierAbilitySystemComponent> Source);

	void TickStealth(float DeltaTime);
	// 활성 소스들의 메시를 다시 모아서 오버라이드를 붙이거나 떼어낸다 ( idempotent ).
	void RefreshStealthMeshOverrides();
	void CollectStealthMeshesFor(
		AActor* Target,
		TArray<UMeshComponent*>& OutFirstPersonMeshes,
		TArray<UMeshComponent*>& OutThirdPersonMeshes) const;
	void ApplyStealthMeshOverride(UMeshComponent* Mesh, UMaterialInterface* GlassMaterial, int32 StencilValue);
	void ClearStealthMeshOverride(UMeshComponent* Mesh);
	void UpdateStealthView();
	float GetStealthFadeDuration() const;
	int32 GetStealthStencilValue() const;
};
