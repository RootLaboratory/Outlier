// Fill out your copyright notice in the Description page of Project Settings.


#include "PostProcess/MaterialPostProcessSubsystem.h"

#include "PostProcess/OutlierPostProcessVolume.h"
#include "PostProcess/OutlierStealthVisualTarget.h"
#include "PostProcess/OutlierSpawnVisualTarget.h"
#include "Components/MeshComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "GameFramework/Pawn.h"
#include "GAS/OutlierAbilitySystemComponent.h"
#include "GameplayTags/OutlierGameplayTags.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"

namespace
{
// BoundPostProcessVolume 이 없을 때 쓰는 값. 볼륨이 있으면 항상 볼륨 값이 우선한다.
constexpr float DefaultStealthFadeDuration = 0.25f;

// 이번 틱에 이 메시가 어떤 머티리얼을 어느 페이드로 물고 있어야 하는지.
struct FOutlierStealthMeshTarget
{
	UMaterialInterface* Material = nullptr;
	float Fade = 0.0f;
};
}

void UMaterialPostProcessSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Refresh();
}

void UMaterialPostProcessSubsystem::Deinitialize()
{
	for (const TPair<TWeakObjectPtr<UOutlierAbilitySystemComponent>, FOutlierStealthSourceState>& Pair
		: StealthSources)
	{
		if (UOutlierAbilitySystemComponent* AbilitySystem = Pair.Key.Get())
		{
			AbilitySystem->RegisterGameplayTagEvent(OutlierGameplayTags::State::Stealthed())
				.Remove(Pair.Value.TagChangedHandle);
		}
	}
	StealthSources.Reset();
	ResetAllPostProcess(false);

	Super::Deinitialize();
}

TStatId UMaterialPostProcessSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UMaterialPostProcessSubsystem, STATGROUP_Tickables);
}

void UMaterialPostProcessSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// 등장 연출은 완료 통보를 서버가 받아야 하므로 데디 서버에서도 시간을 잰다.
	TickEnemySpawnPresentations(DeltaTime);

	if (ShouldSkipRenderingWork())
	{
		return;
	}

	TickStealth(DeltaTime);
}

void UMaterialPostProcessSubsystem::RegisterPostProcessVolume(AOutlierPostProcessVolume* InPostProcessVolume)
{
	if (!InPostProcessVolume)
	{
		return;
	}

	if (!InPostProcessVolume->HasValidScanPostProcessBindings()
		&& !InPostProcessVolume->HasStealthMeshMaterials()
		&& !InPostProcessVolume->HasEnemySpawnDissolveMaterial()
		&& !InPostProcessVolume->HasValidPostProcessMaterial(EOutlierPostProcessMaterialType::Damaged)
		&& !InPostProcessVolume->HasValidPostProcessMaterial(EOutlierPostProcessMaterialType::Magnetic)
		&& !InPostProcessVolume->HasValidPostProcessMaterial(EOutlierPostProcessMaterialType::PartnerOutline))
	{
		return;
	}

	BoundPostProcessVolume = InPostProcessVolume;
	ApplyAlwaysOnPostProcess();
}

void UMaterialPostProcessSubsystem::SetPostProcessEnabled(EOutlierPostProcessMaterialType MaterialType, bool bEnabled)
{
	if (ShouldSkipRenderingWork() || !BoundPostProcessVolume)
	{
		return;
	}

	BoundPostProcessVolume->SetPostProcessEnabled(MaterialType, bEnabled);
}

void UMaterialPostProcessSubsystem::Refresh()
{
	if (!BoundPostProcessVolume)
	{
		return;
	}

	BoundPostProcessVolume->DisableAllBlendablesHard();
	FlushPostProcessMaterialParameters();
	FlushScanStencilRestoreStates();
	// 하드 디스에이블로 상시 패스까지 꺼졌으므로 되살린다.
	ApplyAlwaysOnPostProcess();
}

void UMaterialPostProcessSubsystem::ResetAllPostProcess(bool bRestoreAlwaysOnPostProcess)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(MagneticDisableTimerHandle);
	}
	FlushScanStencilRestoreStates();
	FlushStealthRestoreStates();
	// 등장 디졸브 위에 은신이 적용될 수 있으므로, 은신을 먼저 떼어낸다.
	FlushEnemySpawnPresentations();
	// 구독은 유지하되 이전 은신의 페이드 상태가 다음 틱에 다시 적용되지 않게 한다.
	for (auto& Pair : StealthSources)
	{
		Pair.Value.CurrentFade = 0.0f;
		Pair.Value.TargetFade = 0.0f;
		Pair.Value.bStealthTagActive = false;
	}
	if (IsValid(BoundPostProcessVolume))
	{
		BoundPostProcessVolume->DisableAllBlendablesHard();
		BoundPostProcessVolume->ResetPostProcessMaterialParameters();
		if (bRestoreAlwaysOnPostProcess)
		{
			ApplyAlwaysOnPostProcess();
		}
	}
	UE_LOG(LogTemp, Log, TEXT("[PostProcessReset][Material] Volume=%s RestoreAlwaysOn=%d"),
		*GetNameSafe(BoundPostProcessVolume), bRestoreAlwaysOnPostProcess);
}

void UMaterialPostProcessSubsystem::StartScanPostProcess(
	FVector ScanOrigin,
	float CurrentScanRadius,
	float Range)
{
	if (ShouldSkipRenderingWork() || !BoundPostProcessVolume)
	{
		return;
	}

	BoundPostProcessVolume->SetScanMaterialParameters(ScanOrigin, CurrentScanRadius, Range);
	BoundPostProcessVolume->SetPostProcessEnabled(EOutlierPostProcessMaterialType::Scan ,true);
}

void UMaterialPostProcessSubsystem::UpdateScanPostProcess(
	FVector ScanOrigin,
	float CurrentScanRadius)
{

	if (ShouldSkipRenderingWork() || !BoundPostProcessVolume)
	{
		UE_LOG(LogTemp, Error, TEXT("BoundPostProcessVolume inValid"));
		return;
	}

	BoundPostProcessVolume->UpdateScanMaterialParameters(ScanOrigin, CurrentScanRadius);
}

void UMaterialPostProcessSubsystem::RegisterStealthSource(UOutlierAbilitySystemComponent* AbilitySystem)
{
	if (ShouldSkipRenderingWork() || !AbilitySystem)
	{
		return;
	}

	const TWeakObjectPtr<UOutlierAbilitySystemComponent> SourceKey(AbilitySystem);
	if (StealthSources.Contains(SourceKey))
	{
		return;
	}

	FOutlierStealthSourceState State;
	State.bStealthTagActive =
		AbilitySystem->HasMatchingGameplayTag(OutlierGameplayTags::State::Stealthed());
	State.TargetFade = State.bStealthTagActive ? 1.0f : 0.0f;
	// 이미 은신 중인 폰에 붙는 경우( 리스폰 / 재빙의 )는 페이드 없이 바로 맞춘다.
	State.CurrentFade = State.TargetFade;
	State.TagChangedHandle =
		AbilitySystem->RegisterGameplayTagEvent(OutlierGameplayTags::State::Stealthed())
			.AddUObject(this, &UMaterialPostProcessSubsystem::HandleStealthTagChanged, SourceKey);

	StealthSources.Add(SourceKey, State);
	RefreshStealthMeshOverrides();
}

void UMaterialPostProcessSubsystem::UnregisterStealthSource(UOutlierAbilitySystemComponent* AbilitySystem)
{
	if (!AbilitySystem)
	{
		return;
	}

	const TWeakObjectPtr<UOutlierAbilitySystemComponent> SourceKey(AbilitySystem);
	if (const FOutlierStealthSourceState* State = StealthSources.Find(SourceKey))
	{
		AbilitySystem->RegisterGameplayTagEvent(OutlierGameplayTags::State::Stealthed())
			.Remove(State->TagChangedHandle);
		StealthSources.Remove(SourceKey);
	}

	// 이 소스가 물고 있던 메시는 다음 재수집에서 대상 집합에 안 잡히므로 여기서 복구된다.
	RefreshStealthMeshOverrides();
}

void UMaterialPostProcessSubsystem::HandleStealthTagChanged(
	const FGameplayTag Tag,
	int32 NewCount,
	TWeakObjectPtr<UOutlierAbilitySystemComponent> Source)
{
	(void)Tag;

	FOutlierStealthSourceState* State = StealthSources.Find(Source);
	if (!State)
	{
		return;
	}

	State->bStealthTagActive = NewCount > 0;
	State->TargetFade = State->bStealthTagActive ? 1.0f : 0.0f;

	if (GetStealthFadeDuration() <= 0.0f)
	{
		State->CurrentFade = State->TargetFade;
	}

	// 태그 On 은 즉시 반영해야 첫 프레임에 안 새어 보인다. Off 는 페이드가 0 에 닿을 때
	// TickStealth 가 머티리얼을 마저 원복한다.
	RefreshStealthMeshOverrides();
}

void UMaterialPostProcessSubsystem::TickStealth(float DeltaTime)
{
	if (StealthSources.IsEmpty() && StealthMeshRestoreStates.IsEmpty())
	{
		return;
	}

	const float FadeDuration = GetStealthFadeDuration();
	const float FadeSpeed = FadeDuration > 0.0f ? 1.0f / FadeDuration : 0.0f;

	bool bAnyStealthActive = false;
	for (auto SourceIt = StealthSources.CreateIterator(); SourceIt; ++SourceIt)
	{
		// ClearForActor 없이 사라진 ASC( 강제 파괴 등 )는 여기서 정리한다.
		if (!SourceIt.Key().IsValid())
		{
			SourceIt.RemoveCurrent();
			continue;
		}

		FOutlierStealthSourceState& State = SourceIt.Value();
		if (FMath::IsNearlyEqual(State.CurrentFade, State.TargetFade))
		{
			State.CurrentFade = State.TargetFade;
		}
		else
		{
			State.CurrentFade = FadeSpeed > 0.0f
				? FMath::FInterpConstantTo(State.CurrentFade, State.TargetFade, DeltaTime, FadeSpeed)
				: State.TargetFade;
		}

		bAnyStealthActive |= State.bStealthTagActive || State.CurrentFade > 0.0f;
	}

	// 은신이 하나도 안 걸려 있고 되돌릴 것도 없으면 여기서 끝 ( 평상시 비용은 위 루프뿐 ).
	if (!bAnyStealthActive && StealthMeshRestoreStates.IsEmpty())
	{
		return;
	}

	// 대상 집합은 매 틱 다시 모은다. 무기 교체 / 파트너 교체 / 폰 리스폰이 별도 신호 없이 따라온다.
	// 페이드 갱신도 여기서 같이 이뤄진다 ( 소스별 CurrentFade 를 그 소스의 메시 MID 에 밀어 넣는다 ).
	RefreshStealthMeshOverrides();
}

void UMaterialPostProcessSubsystem::RefreshStealthMeshOverrides()
{
	UMaterialInterface* FirstPersonMaterial = GetFirstPersonStealthMaterial();
	UMaterialInterface* ThirdPersonMaterial = GetThirdPersonStealthMaterial();

	// 이번에 오버라이드가 유지돼야 하는 메시 집합을 다시 만든다.
	TMap<UMeshComponent*, FOutlierStealthMeshTarget> DesiredMeshes;
	for (const TPair<TWeakObjectPtr<UOutlierAbilitySystemComponent>, FOutlierStealthSourceState>& Pair
		: StealthSources)
	{
		const UOutlierAbilitySystemComponent* AbilitySystem = Pair.Key.Get();
		const FOutlierStealthSourceState& State = Pair.Value;
		if (!AbilitySystem)
		{
			continue;
		}

		// 태그가 꺼져도 페이드가 0 에 닿을 때까지는 물고 있어야 페이드 아웃이 보인다.
		if (!State.bStealthTagActive && State.CurrentFade <= 0.0f)
		{
			continue;
		}

		AActor* Avatar = AbilitySystem->GetAvatarActor();
		if (!Avatar)
		{
			continue;
		}

		ScratchFirstPersonMeshes.Reset();
		ScratchThirdPersonMeshes.Reset();
		CollectStealthMeshesFor(Avatar, ScratchFirstPersonMeshes, ScratchThirdPersonMeshes);

		const float Fade = EvaluateStealthFade(State.CurrentFade);

		if (FirstPersonMaterial)
		{
			for (UMeshComponent* Mesh : ScratchFirstPersonMeshes)
			{
				if (Mesh)
				{
					DesiredMeshes.Add(Mesh, FOutlierStealthMeshTarget{ FirstPersonMaterial, Fade });
				}
			}
		}

		if (ThirdPersonMaterial)
		{
			for (UMeshComponent* Mesh : ScratchThirdPersonMeshes)
			{
				if (Mesh)
				{
					DesiredMeshes.Add(Mesh, FOutlierStealthMeshTarget{ ThirdPersonMaterial, Fade });
				}
			}
		}
	}

	// 집합에서 빠진 메시( 교체된 무기, 은신 종료, 파괴된 액터 )와
	// 꽂아야 할 머티리얼이 바뀐 메시를 원상복구한다. 후자는 아래에서 새 머티리얼로 다시 붙는다.
	ScratchStaleMeshes.Reset();
	for (const TPair<TObjectPtr<UMeshComponent>, FOutlierStealthMeshRestoreState>& Pair
		: StealthMeshRestoreStates)
	{
		const FOutlierStealthMeshTarget* Desired = Pair.Key
			? DesiredMeshes.Find(Pair.Key.Get())
			: nullptr;

		if (!Desired || Desired->Material != Pair.Value.SourceMaterial)
		{
			ScratchStaleMeshes.Add(Pair.Key);
		}
	}
	for (const TObjectPtr<UMeshComponent>& StaleMesh : ScratchStaleMeshes)
	{
		ClearStealthMeshOverride(StaleMesh);
	}

	for (const TPair<UMeshComponent*, FOutlierStealthMeshTarget>& Desired : DesiredMeshes)
	{
		ApplyStealthMeshOverride(Desired.Key, Desired.Value.Material);
		SetStealthMeshFade(Desired.Key, Desired.Value.Fade);
	}
}

void UMaterialPostProcessSubsystem::CollectStealthMeshesFor(
	AActor* Target,
	TArray<UMeshComponent*>& OutFirstPersonMeshes,
	TArray<UMeshComponent*>& OutThirdPersonMeshes) const
{
	if (!Target)
	{
		return;
	}

	// 1인칭/3인칭이 섞여 있는 액터만 인터페이스로 답한다.
	if (const IOutlierStealthVisualTarget* StealthTarget = Cast<IOutlierStealthVisualTarget>(Target))
	{
		StealthTarget->CollectStealthMeshes(OutFirstPersonMeshes, OutThirdPersonMeshes);
		return;
	}

	// 그 외에는 전 메시를 3인칭 취급 ( ApplyScanStencil 과 동일한 기본 규칙 ).
	TArray<UMeshComponent*> MeshComponents;
	Target->GetComponents<UMeshComponent>(MeshComponents);
	OutThirdPersonMeshes.Append(MeshComponents);
}

void UMaterialPostProcessSubsystem::ApplyStealthMeshOverride(
	UMeshComponent* Mesh,
	UMaterialInterface* StealthMaterial)
{
	if (!Mesh || !StealthMaterial || StealthMeshRestoreStates.Contains(Mesh))
	{
		return;
	}

	// 페이드 스칼라를 넣어야 하므로 에셋 원본이 아니라 MID 를 꽂는다.
	UMaterialInstanceDynamic* RuntimeMaterial = UMaterialInstanceDynamic::Create(StealthMaterial, this);
	if (!RuntimeMaterial)
	{
		return;
	}

	FOutlierStealthMeshRestoreState RestoreState;
	RestoreState.SourceMaterial = StealthMaterial;
	RestoreState.AppliedMaterial = RuntimeMaterial;

	const int32 MaterialCount = Mesh->GetNumMaterials();
	RestoreState.Materials.Reserve(MaterialCount);
	for (int32 MaterialIndex = 0; MaterialIndex < MaterialCount; ++MaterialIndex)
	{
		RestoreState.Materials.Add(Mesh->GetMaterial(MaterialIndex));
		Mesh->SetMaterial(MaterialIndex, RuntimeMaterial);
	}

	StealthMeshRestoreStates.Add(Mesh, MoveTemp(RestoreState));
}

void UMaterialPostProcessSubsystem::ClearStealthMeshOverride(UMeshComponent* Mesh)
{
	FOutlierStealthMeshRestoreState RestoreState;
	if (!StealthMeshRestoreStates.RemoveAndCopyValue(Mesh, RestoreState))
	{
		return;
	}

	if (!Mesh)
	{
		return;
	}

	for (int32 MaterialIndex = 0; MaterialIndex < RestoreState.Materials.Num(); ++MaterialIndex)
	{
		Mesh->SetMaterial(MaterialIndex, RestoreState.Materials[MaterialIndex]);
	}
}

void UMaterialPostProcessSubsystem::SetStealthMeshFade(UMeshComponent* Mesh, float Fade)
{
	const FOutlierStealthMeshRestoreState* RestoreState = StealthMeshRestoreStates.Find(Mesh);
	if (!RestoreState || !RestoreState->AppliedMaterial)
	{
		return;
	}

	const FName FadeParameterName = GetStealthFadeParameterName();
	if (FadeParameterName.IsNone())
	{
		return;
	}

	// 해당 파라미터가 없는 머티리얼이면 엔진이 무시한다 ( 이 경우 on/off 로만 보인다 ).
	RestoreState->AppliedMaterial->SetScalarParameterValue(FadeParameterName, Fade);
}

void UMaterialPostProcessSubsystem::FlushStealthRestoreStates()
{
	ScratchStaleMeshes.Reset();
	StealthMeshRestoreStates.GetKeys(ScratchStaleMeshes);
	for (const TObjectPtr<UMeshComponent>& Mesh : ScratchStaleMeshes)
	{
		ClearStealthMeshOverride(Mesh);
	}
	StealthMeshRestoreStates.Reset();
}

float UMaterialPostProcessSubsystem::GetStealthFadeDuration() const
{
	return BoundPostProcessVolume
		? FMath::Max(BoundPostProcessVolume->StealthFadeDuration, 0.0f)
		: DefaultStealthFadeDuration;
}

float UMaterialPostProcessSubsystem::EvaluateStealthFade(float LinearFade) const
{
	return BoundPostProcessVolume
		? BoundPostProcessVolume->EvaluateStealthFade(LinearFade)
		: FMath::Clamp(LinearFade, 0.0f, 1.0f);
}

FName UMaterialPostProcessSubsystem::GetStealthFadeParameterName() const
{
	return BoundPostProcessVolume
		? BoundPostProcessVolume->StealthFadeParameterName
		: NAME_None;
}

UMaterialInterface* UMaterialPostProcessSubsystem::GetFirstPersonStealthMaterial() const
{
	return BoundPostProcessVolume
		? BoundPostProcessVolume->GetFirstPersonStealthGlassMaterial()
		: nullptr;
}

UMaterialInterface* UMaterialPostProcessSubsystem::GetThirdPersonStealthMaterial() const
{
	if (!BoundPostProcessVolume)
	{
		return nullptr;
	}

	// 3인칭 전용 머티리얼이 없으면 1인칭 것을 그대로 쓴다.
	UMaterialInterface* ThirdPersonMaterial = BoundPostProcessVolume->GetThirdPersonStealthMaterial();
	return ThirdPersonMaterial
		? ThirdPersonMaterial
		: BoundPostProcessVolume->GetFirstPersonStealthGlassMaterial();
}

void UMaterialPostProcessSubsystem::UpdateDamagedPostProcess(float InHPRatio)
{
	if (ShouldSkipRenderingWork() || !BoundPostProcessVolume)
	{
		return;
	}

	BoundPostProcessVolume->UpdateDamagedMaterialParameters(InHPRatio);
}

void UMaterialPostProcessSubsystem::UpdateDamagedPostProcess(float InHPRatio, FVector4 Color)
{
	if (ShouldSkipRenderingWork() || !BoundPostProcessVolume)
	{
		return;
	}

	BoundPostProcessVolume->UpdateDamagedMaterialParameters(InHPRatio, Color);
}

void UMaterialPostProcessSubsystem::EndScanPostProcess()
{
	if (ShouldSkipRenderingWork())
	{
		return;
	}

	if (BoundPostProcessVolume)
	{
		BoundPostProcessVolume->SetPostProcessEnabled(EOutlierPostProcessMaterialType::Scan, false);
		BoundPostProcessVolume->SetScanMaterialParameters(FVector::ZeroVector, 0.0f, 0.0f);
	}
	ClearAllScanStencils();
}

void UMaterialPostProcessSubsystem::EndDamagedPostProcess()
{
	if (ShouldSkipRenderingWork())
	{
		return;
	}

	if (BoundPostProcessVolume)
	{
		BoundPostProcessVolume->SetPostProcessEnabled(EOutlierPostProcessMaterialType::Damaged, false);
		BoundPostProcessVolume->UpdateDamagedMaterialParameters(1.0f);
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!BoundPostProcessVolume"))

	}
}

void UMaterialPostProcessSubsystem::StartMagneticPostProcess(FVector Origin, float Radius, float Duration)
{
	UWorld* World = GetWorld();
	if (ShouldSkipRenderingWork() || !World || !BoundPostProcessVolume)
	{
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("[MagneticLens] 2/3 중단. DedicatedServer=%s World=%s BoundVolume=%s")
			TEXT(" ( 볼륨이 null 이면 레벨에 AOutlierPostProcessVolume 이 없거나 BeginPlay 등록이 안 된 것 )"),
			ShouldSkipRenderingWork() ? TEXT("true") : TEXT("false"),
			World ? TEXT("valid") : TEXT("null"),
			*GetNameSafe(BoundPostProcessVolume));
		return;
	}

	// 이전 펄스의 페이드아웃 예약이 남아 있으면 취소한다. 안 그러면 새 펄스를 도중에 꺼 버린다.
	World->GetTimerManager().ClearTimer(MagneticDisableTimerHandle);

	const float StartTime = World->GetTimeSeconds();

	UE_LOG(
		LogTemp,
		Warning,
		TEXT("[MagneticLens] 2/3 서브시스템 통과. Volume=%s HasMagneticMaterial=%s Origin=%s Radius=%.1f StartTime=%.2f EndTime=%.2f"),
		*GetNameSafe(BoundPostProcessVolume),
		BoundPostProcessVolume->HasValidPostProcessMaterial(EOutlierPostProcessMaterialType::Magnetic)
			? TEXT("true") : TEXT("false"),
		*Origin.ToCompactString(),
		Radius,
		StartTime,
		StartTime + FMath::Max(Duration, 0.0f));
	BoundPostProcessVolume->SetMagneticMaterialParameters(
		Origin,
		Radius,
		StartTime,
		StartTime + FMath::Max(Duration, 0.0f)
	);
	BoundPostProcessVolume->SetPostProcessEnabled(EOutlierPostProcessMaterialType::Magnetic, true);
}

void UMaterialPostProcessSubsystem::EndMagneticPostProcess()
{
	UWorld* World = GetWorld();
	if (ShouldSkipRenderingWork() || !World || !BoundPostProcessVolume)
	{
		return;
	}

	// 즉시 끊지 않는다. EndTime 을 지금으로 당겨서 머티리얼이 페이드아웃을 시작하게 하고,
	// 그게 끝난 뒤에야 블렌더블 가중치를 내려서 풀스크린 패스를 끊는다.
	BoundPostProcessVolume->BeginMagneticFadeOut(World->GetTimeSeconds());

	const float FadeOutDuration = FMath::Max(BoundPostProcessVolume->MagneticFadeOutDuration, 0.0f);
	if (FadeOutDuration <= 0.0f)
	{
		World->GetTimerManager().ClearTimer(MagneticDisableTimerHandle);
		BoundPostProcessVolume->SetPostProcessEnabled(EOutlierPostProcessMaterialType::Magnetic, false);
		BoundPostProcessVolume->ResetMagneticMaterialParameters();
		return;
	}

	TWeakObjectPtr<UMaterialPostProcessSubsystem> WeakThis(this);
	World->GetTimerManager().SetTimer(
		MagneticDisableTimerHandle,
		FTimerDelegate::CreateLambda([WeakThis]()
		{
			UMaterialPostProcessSubsystem* Self = WeakThis.Get();
			if (!Self || !Self->BoundPostProcessVolume)
			{
				return;
			}

			Self->BoundPostProcessVolume->SetPostProcessEnabled(EOutlierPostProcessMaterialType::Magnetic, false);
			Self->BoundPostProcessVolume->ResetMagneticMaterialParameters();
		}),
		FadeOutDuration,
		false
	);
}

void UMaterialPostProcessSubsystem::StartEnemySpawnPresentation(AActor* Target, FSimpleDelegate OnFinished)
{
	if (!Target)
	{
		OnFinished.ExecuteIfBound();
		return;
	}

	// 같은 대상의 이전 연출이 남아 있으면 원복부터 하고 처음부터 다시 잰다.
	StopEnemySpawnPresentation(Target);

	const float Duration = BoundPostProcessVolume
		? BoundPostProcessVolume->GetEnemySpawnPresentationDuration()
		: 0.0f;
	if (Duration <= KINDA_SMALL_NUMBER)
	{
		OnFinished.ExecuteIfBound();
		return;
	}

	FOutlierSpawnPresentationState& State = SpawnPresentations.AddDefaulted_GetRef();
	State.Target = Target;
	State.Duration = Duration;
	State.OnFinished = MoveTemp(OnFinished);
	ApplyEnemySpawnPresentationMaterials(State, Target);
}

void UMaterialPostProcessSubsystem::StopEnemySpawnPresentation(AActor* Target)
{
	// EndPlay 중인 액터도 찾아야 하므로 Garbage 표시 여부와 무관하게 비교한다.
	const int32 StateIndex = SpawnPresentations.IndexOfByPredicate(
		[Target](const FOutlierSpawnPresentationState& State)
		{
			return State.Target.Get(true) == Target;
		});
	if (StateIndex == INDEX_NONE)
	{
		return;
	}

	RestoreEnemySpawnPresentationMaterials(SpawnPresentations[StateIndex]);
	SpawnPresentations.RemoveAtSwap(StateIndex);
}

void UMaterialPostProcessSubsystem::TickEnemySpawnPresentations(float DeltaTime)
{
	if (SpawnPresentations.IsEmpty())
	{
		return;
	}

	// 완료 통보가 다른 연출을 시작 / 중단할 수 있으므로 순회가 끝난 뒤에 부른다.
	TArray<FSimpleDelegate, TInlineAllocator<4>> FinishedCallbacks;
	for (int32 StateIndex = SpawnPresentations.Num() - 1; StateIndex >= 0; --StateIndex)
	{
		FOutlierSpawnPresentationState& State = SpawnPresentations[StateIndex];
		if (!State.Target.IsValid())
		{
			RestoreEnemySpawnPresentationMaterials(State);
			SpawnPresentations.RemoveAtSwap(StateIndex);
			continue;
		}

		State.ElapsedTime += DeltaTime;
		if (State.ElapsedTime < State.Duration)
		{
			if (State.AppliedMaterial && BoundPostProcessVolume)
			{
				const float Progress = State.ElapsedTime / State.Duration;
				BoundPostProcessVolume->SetEnemySpawnDissolveAmount(
					State.AppliedMaterial,
					FMath::Lerp(State.StartAmount, State.EndAmount, Progress));
			}
			continue;
		}

		// 끝 값에서는 모든 픽셀이 불투명이고 경계 글로우 / 글리치가 0 이므로 바로 원본으로 되돌린다.
		RestoreEnemySpawnPresentationMaterials(State);
		FinishedCallbacks.Add(MoveTemp(State.OnFinished));
		SpawnPresentations.RemoveAtSwap(StateIndex);
	}

	for (FSimpleDelegate& FinishedCallback : FinishedCallbacks)
	{
		FinishedCallback.ExecuteIfBound();
	}
}

void UMaterialPostProcessSubsystem::ApplyEnemySpawnPresentationMaterials(
	FOutlierSpawnPresentationState& State,
	AActor* Target)
{
	if (ShouldSkipRenderingWork() || !BoundPostProcessVolume)
	{
		return;
	}

	UMaterialInterface* DissolveMaterial = BoundPostProcessVolume->GetEnemySpawnDissolveMaterial();
	const IOutlierSpawnVisualTarget* SpawnTarget = Cast<IOutlierSpawnVisualTarget>(Target);
	if (!DissolveMaterial || !SpawnTarget
		|| !BoundPostProcessVolume->ComputeEnemySpawnDissolveRange(State.StartAmount, State.EndAmount))
	{
		return;
	}

	ScratchSpawnPresentationMeshes.Reset();
	SpawnTarget->CollectSpawnPresentationMeshes(ScratchSpawnPresentationMeshes);
	if (ScratchSpawnPresentationMeshes.IsEmpty())
	{
		return;
	}

	// 디졸브 값을 넣어야 하므로 에셋 원본이 아니라 MID 를 꽂는다.
	UMaterialInstanceDynamic* RuntimeMaterial = UMaterialInstanceDynamic::Create(DissolveMaterial, this);
	if (!RuntimeMaterial)
	{
		return;
	}

	// 교체한 프레임부터 전부 투명이어야 원본 메시가 한 번 비치지 않는다.
	BoundPostProcessVolume->SetEnemySpawnDissolveAmount(RuntimeMaterial, State.StartAmount);
	State.AppliedMaterial = RuntimeMaterial;

	for (UMeshComponent* Mesh : ScratchSpawnPresentationMeshes)
	{
		// 같은 메시를 두 번 받으면 MID 를 원본으로 저장하게 되므로 걸러낸다.
		if (!Mesh || State.Meshes.ContainsByPredicate(
			[Mesh](const FOutlierSpawnPresentationMeshRestoreState& MeshState)
			{
				return MeshState.Mesh.Get() == Mesh;
			}))
		{
			continue;
		}

		FOutlierSpawnPresentationMeshRestoreState& MeshState = State.Meshes.AddDefaulted_GetRef();
		MeshState.Mesh = Mesh;
		const int32 MaterialCount = Mesh->GetNumMaterials();
		MeshState.Materials.Reserve(MaterialCount);
		for (int32 MaterialIndex = 0; MaterialIndex < MaterialCount; ++MaterialIndex)
		{
			MeshState.Materials.Add(Mesh->GetMaterial(MaterialIndex));
			Mesh->SetMaterial(MaterialIndex, RuntimeMaterial);
		}
	}
}

void UMaterialPostProcessSubsystem::RestoreEnemySpawnPresentationMaterials(FOutlierSpawnPresentationState& State)
{
	UMaterialInterface* AppliedMaterial = State.AppliedMaterial;
	for (const FOutlierSpawnPresentationMeshRestoreState& MeshState : State.Meshes)
	{
		UMeshComponent* Mesh = MeshState.Mesh.Get();
		if (!Mesh)
		{
			continue;
		}

		for (int32 MaterialIndex = 0; MaterialIndex < MeshState.Materials.Num(); ++MaterialIndex)
		{
			// 연출 도중 다른 쪽이 바꿔 끼운 슬롯은 건드리지 않는다.
			if (Mesh->GetMaterial(MaterialIndex) == AppliedMaterial)
			{
				Mesh->SetMaterial(MaterialIndex, MeshState.Materials[MaterialIndex]);
			}
		}
	}

	State.Meshes.Reset();
	State.AppliedMaterial = nullptr;
}

void UMaterialPostProcessSubsystem::FlushEnemySpawnPresentations()
{
	// 월드 종료 정리용이라 완료 통보는 보내지 않는다.
	for (FOutlierSpawnPresentationState& State : SpawnPresentations)
	{
		RestoreEnemySpawnPresentationMaterials(State);
	}
	SpawnPresentations.Reset();
}

void UMaterialPostProcessSubsystem::ApplyScanStencil(AActor* Actor, int32 StencilValue)
{
	if (ShouldSkipRenderingWork() || !Actor)
	{
		return;
	}

	TArray<UPrimitiveComponent*> PrimitiveComponents;
	Actor->GetComponents<UPrimitiveComponent>(PrimitiveComponents);

	//BP에서 설정하면 상관없긴 하다만.

	for (UPrimitiveComponent* PrimitiveComponent : PrimitiveComponents)
	{
		if (!PrimitiveComponent)
		{
			continue;
		}

		TWeakObjectPtr<UPrimitiveComponent> ComponentKey(PrimitiveComponent);
		if (!ScanStencilRestoreStates.Contains(ComponentKey))
		{
			FScanStencilRestoreState RestoreState;
			RestoreState.bRenderCustomDepth = PrimitiveComponent->bRenderCustomDepth;
			RestoreState.CustomDepthStencilValue = PrimitiveComponent->CustomDepthStencilValue;
			ScanStencilRestoreStates.Add(ComponentKey, RestoreState);
		}

		PrimitiveComponent->SetRenderCustomDepth(true);
		PrimitiveComponent->SetCustomDepthStencilValue(StencilValue);
	}
}

void UMaterialPostProcessSubsystem::ClearScanStencil(AActor* Actor)
{
	if (ShouldSkipRenderingWork() || !Actor)
	{
		return;
	}

	TArray<UPrimitiveComponent*> PrimitiveComponents;
	Actor->GetComponents<UPrimitiveComponent>(PrimitiveComponents);

	for (UPrimitiveComponent* PrimitiveComponent : PrimitiveComponents)
	{
		if (!PrimitiveComponent)
		{
			continue;
		}

		TWeakObjectPtr<UPrimitiveComponent> ComponentKey(PrimitiveComponent);
		if (const FScanStencilRestoreState* RestoreState = ScanStencilRestoreStates.Find(ComponentKey))
		{
			PrimitiveComponent->SetRenderCustomDepth(RestoreState->bRenderCustomDepth);
			PrimitiveComponent->SetCustomDepthStencilValue(RestoreState->CustomDepthStencilValue);
			ScanStencilRestoreStates.Remove(ComponentKey);
		}
	}
}

void UMaterialPostProcessSubsystem::ApplyAlwaysOnPostProcess()
{
	if (ShouldSkipRenderingWork() || !BoundPostProcessVolume)
	{
		return;
	}

	// 파트너 아웃라인은 스캔과 달리 게임플레이 이벤트가 켜고 끄지 않는다.
	// 머티리얼이 안 꽂혀 있으면 SetBlendableWeight 가 어차피 실패하지만,
	// 의도를 분명히 하려고 여기서 먼저 거른다.
	if (!BoundPostProcessVolume->HasValidPostProcessMaterial(EOutlierPostProcessMaterialType::PartnerOutline))
	{
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("[PartnerOutline] %s 에 PartnerOutline 머티리얼이 없다. 볼륨의 PostProcessMaterials 맵을 확인."),
			*GetNameSafe(BoundPostProcessVolume));
		return;
	}

	BoundPostProcessVolume->SetPostProcessEnabled(EOutlierPostProcessMaterialType::PartnerOutline, true);
}

bool UMaterialPostProcessSubsystem::ShouldSkipRenderingWork() const
{
	const UWorld* World = GetWorld();
	return !World || World->GetNetMode() == NM_DedicatedServer;
}

void UMaterialPostProcessSubsystem::ClearAllScanStencils()
{
	for (const TPair<TWeakObjectPtr<UPrimitiveComponent>, FScanStencilRestoreState>& Pair : ScanStencilRestoreStates)
	{
		if (UPrimitiveComponent* PrimitiveComponent = Pair.Key.Get())
		{
			PrimitiveComponent->SetRenderCustomDepth(Pair.Value.bRenderCustomDepth);
			PrimitiveComponent->SetCustomDepthStencilValue(Pair.Value.CustomDepthStencilValue);
		}
	}

	ScanStencilRestoreStates.Reset();
}

void UMaterialPostProcessSubsystem::DisableAllBoundPostProcessMaterials()
{
	if (!BoundPostProcessVolume)
	{
		return;
	}

	for (const TPair<EOutlierPostProcessMaterialType, TObjectPtr<UMaterialInterface>>& Pair
		: BoundPostProcessVolume->PostProcessMaterials)
	{
		BoundPostProcessVolume->SetPostProcessEnabled(Pair.Key, false);
	}

}

void UMaterialPostProcessSubsystem::FlushScanStencilRestoreStates()
{
	for (const TPair<TWeakObjectPtr<UPrimitiveComponent>, FScanStencilRestoreState>& Pair
		: ScanStencilRestoreStates)
	{
		if (UPrimitiveComponent* PrimitiveComponent = Pair.Key.Get())
		{
			PrimitiveComponent->SetRenderCustomDepth(Pair.Value.bRenderCustomDepth);
			PrimitiveComponent->SetCustomDepthStencilValue(Pair.Value.CustomDepthStencilValue);
		}
	}

	ScanStencilRestoreStates.Reset();
}

void UMaterialPostProcessSubsystem::FlushPostProcessMaterialParameters()
{
	if (!BoundPostProcessVolume)
	{
		return;
	}

	BoundPostProcessVolume->ResetPostProcessMaterialParameters();
}
