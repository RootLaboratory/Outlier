// Fill out your copyright notice in the Description page of Project Settings.


#include "PostProcess/MaterialPostProcessSubsystem.h"

#include "PostProcess/OutlierPostProcessVolume.h"
#include "PostProcess/OutlierStealthVisualTarget.h"
#include "Components/MeshComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/World.h"
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
	FlushStealthRestoreStates();

	Super::Deinitialize();
	Refresh();
}

TStatId UMaterialPostProcessSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UMaterialPostProcessSubsystem, STATGROUP_Tickables);
}

void UMaterialPostProcessSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

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
		&& !InPostProcessVolume->HasValidPostProcessMaterial(EOutlierPostProcessMaterialType::Damaged))
	{
		return;
	}

	BoundPostProcessVolume = InPostProcessVolume;
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
