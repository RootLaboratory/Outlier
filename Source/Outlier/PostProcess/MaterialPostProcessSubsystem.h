// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Subsystems/WorldSubsystem.h"
#include "MaterialPostProcessSubsystem.generated.h"

class AActor;
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

// 등장 연출 중인 메시 1개의 원본 슬롯 머티리얼.
USTRUCT()
struct FOutlierSpawnPresentationMeshRestoreState
{
	GENERATED_BODY()

	UPROPERTY()
	TWeakObjectPtr<UMeshComponent> Mesh;

	// 교체 전 슬롯별 원본. 동적 머티리얼이면 여기서만 참조될 수 있으므로 GC 로부터 지킨다.
	UPROPERTY()
	TArray<TObjectPtr<UMaterialInterface>> Materials;
};

// 등장 연출 대상 1개분 상태. 한 대상의 모든 메시가 같은 MID 를 공유한다 ( 진행도가 같다 ).
USTRUCT()
struct FOutlierSpawnPresentationState
{
	GENERATED_BODY()

	UPROPERTY()
	TWeakObjectPtr<AActor> Target;

	// 데디 서버 / 머티리얼 미지정이면 비어 있고 시간만 잰다.
	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> AppliedMaterial;

	UPROPERTY()
	TArray<FOutlierSpawnPresentationMeshRestoreState> Meshes;

	float ElapsedTime = 0.0f;
	float Duration = 0.0f;
	float StartAmount = 0.0f;
	float EndAmount = 0.0f;
	FSimpleDelegate OnFinished;
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
	// 볼륨 미등록 상태에서도 타이머와 메시/스텐실의 원본 복구 상태를 정리한다.
	UFUNCTION(BlueprintCallable, Category = "PostProcess")
	void ResetAllPostProcess(bool bRestoreAlwaysOnPostProcess = true);
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

	// Enemy Spawn
	// 등장 디졸브. 대상은 IOutlierSpawnVisualTarget 으로 메시만 답하고,
	// 머티리얼 / 연출 시간 / Dissolve Amount 범위는 볼륨이, 교체 / 진행 / 원복은 이 서브시스템이 전담한다.
	// 연출 시간이 끝나면( 디졸브가 끝 값에 닿으면 ) 원본 머티리얼을 되돌린 뒤 OnFinished 를 부른다.
	// 볼륨이 없거나 연출 시간이 0 이면 OnFinished 를 즉시 부른다.
	// 데디 서버는 머티리얼 없이 시간만 잰다 ( 완료 통보는 서버가 받아야 한다 ).
	void StartEnemySpawnPresentation(AActor* Target, FSimpleDelegate OnFinished);
	// 완료 통보 없이 바로 원복한다. 연출 도중 상태가 바뀌거나 액터가 사라질 때 쓴다.
	void StopEnemySpawnPresentation(AActor* Target);

	UPROPERTY()
	TObjectPtr<AOutlierPostProcessVolume> BoundPostProcessVolume;

private:
	friend class FOutlierMaterialPostProcessResetTest;

	TMap<TWeakObjectPtr<UPrimitiveComponent>, FScanStencilRestoreState> ScanStencilRestoreStates;

	// 페이드아웃이 끝난 뒤 자기장 블렌더블을 내리는 일회성 타이머 ( 매 틱 폴링이 아니다 ).
	FTimerHandle MagneticDisableTimerHandle;

	// 오버라이드가 걸려 있는 메시와 그 원본. 원본 머티리얼을 GC 로부터 지켜야 하므로 UPROPERTY.
	UPROPERTY()
	TMap<TObjectPtr<UMeshComponent>, FOutlierStealthMeshRestoreState> StealthMeshRestoreStates;

	TMap<TWeakObjectPtr<UOutlierAbilitySystemComponent>, FOutlierStealthSourceState> StealthSources;

	// 동시에 등장하는 적은 한 웨이브 수준이라 배열 선형 탐색으로 충분하다.
	UPROPERTY()
	TArray<FOutlierSpawnPresentationState> SpawnPresentations;
	TArray<UMeshComponent*> ScratchSpawnPresentationMeshes;

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
	void TickEnemySpawnPresentations(float DeltaTime);
	void ApplyEnemySpawnPresentationMaterials(FOutlierSpawnPresentationState& State, AActor* Target);
	void RestoreEnemySpawnPresentationMaterials(FOutlierSpawnPresentationState& State);
	void FlushEnemySpawnPresentations();

	float GetStealthFadeDuration() const;
	float EvaluateStealthFade(float LinearFade) const;
	FName GetStealthFadeParameterName() const;
	UMaterialInterface* GetFirstPersonStealthMaterial() const;
	UMaterialInterface* GetThirdPersonStealthMaterial() const;
};
