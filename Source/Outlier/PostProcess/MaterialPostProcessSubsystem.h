// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Subsystems/WorldSubsystem.h"
#include "MaterialPostProcessSubsystem.generated.h"

class AOutlierPostProcessVolume;
class UMaterialInstanceDynamic;
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
// 1인칭 / 3인칭 구분 없이 슬롯별 머티리얼을 교체하므로 두 경우 모두 채워진다.
USTRUCT()
struct FOutlierStealthMeshRestoreState
{
	GENERATED_BODY()

	// 교체 전 슬롯별 원본 머티리얼.
	UPROPERTY()
	TArray<TObjectPtr<UMaterialInterface>> Materials;

	// 지금 꽂혀 있는 MID. 페이드 스칼라를 매 틱 여기에 밀어 넣는다.
	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> AppliedMaterial;

	// AppliedMaterial 을 만든 원본. 볼륨 쪽 지정이 바뀌면 다시 만들어야 하므로 들고 있는다.
	UPROPERTY()
	TObjectPtr<UMaterialInterface> SourceMaterial;
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
	// 1인칭 / 3인칭 모두 메시 머티리얼 교체로 처리한다 ( 포스트프로세스 / 스텐실 미사용 ).
	// 3인칭 메시는 자기 화면엔 안 보이고 상대 화면에만 보이므로, 로컬 여부로 거르지 않는다.
	void RegisterStealthSource(UOutlierAbilitySystemComponent* AbilitySystem);
	void UnregisterStealthSource(UOutlierAbilitySystemComponent* AbilitySystem);
	void FlushStealthRestoreStates();

	//Damaged
	void UpdateDamagedPostProcess(float InHPRatio);
	void UpdateDamagedPostProcess(float InHPRatio, FVector4 Color);
	void EndDamagedPostProcess();

	// Magnetic
	// 자기장 발생기의 중력렌즈. 원점 / 반경이 펄스 동안 고정이므로 Update 가 없다.
	// 페이드 엔벨로프와 노이즈는 머티리얼이 Time 노드로 직접 굴린다 ( 서브시스템 틱 부하 0 ).
	void StartMagneticPostProcess(FVector Origin, float Radius, float Duration);
	void EndMagneticPostProcess();

	UPROPERTY()
	TObjectPtr<AOutlierPostProcessVolume> BoundPostProcessVolume;

private:
	TMap<TWeakObjectPtr<UPrimitiveComponent>, FScanStencilRestoreState> ScanStencilRestoreStates;

	// 페이드아웃이 끝난 뒤 자기장 블렌더블을 내리는 일회성 타이머 ( 매 틱 폴링이 아니다 ).
	FTimerHandle MagneticDisableTimerHandle;

	// 오버라이드가 걸려 있는 메시와 그 원본. 원본 머티리얼을 GC 로부터 지켜야 하므로 UPROPERTY.
	UPROPERTY()
	TMap<TObjectPtr<UMeshComponent>, FOutlierStealthMeshRestoreState> StealthMeshRestoreStates;

	TMap<TWeakObjectPtr<UOutlierAbilitySystemComponent>, FOutlierStealthSourceState> StealthSources;

	// 매 틱 재사용하는 스크래치 ( 할당 방지 ).
	TArray<UMeshComponent*> ScratchFirstPersonMeshes;
	TArray<UMeshComponent*> ScratchThirdPersonMeshes;
	TArray<TObjectPtr<UMeshComponent>> ScratchStaleMeshes;

	bool ShouldSkipRenderingWork() const;
	// 게임플레이 이벤트와 무관하게 항상 켜져 있어야 하는 패스( 파트너 아웃라인 )를 다시 올린다.
	// DisableAllBlendablesHard 가 전부 0 으로 내리므로 그 뒤에 반드시 호출해야 한다.
	void ApplyAlwaysOnPostProcess();
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
	void ApplyStealthMeshOverride(UMeshComponent* Mesh, UMaterialInterface* StealthMaterial);
	void ClearStealthMeshOverride(UMeshComponent* Mesh);
	// 이미 꽂혀 있는 MID 의 페이드 스칼라만 갱신한다.
	void SetStealthMeshFade(UMeshComponent* Mesh, float Fade);
	float GetStealthFadeDuration() const;
	float EvaluateStealthFade(float LinearFade) const;
	FName GetStealthFadeParameterName() const;
	UMaterialInterface* GetFirstPersonStealthMaterial() const;
	UMaterialInterface* GetThirdPersonStealthMaterial() const;
};
