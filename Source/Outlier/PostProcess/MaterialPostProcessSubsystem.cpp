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

namespace
{
// BoundPostProcessVolume 이 없을 때 쓰는 값. 볼륨이 있으면 항상 볼륨 값이 우선한다.
constexpr int32 DefaultStealthStencilValue = 5;
constexpr float DefaultStealthFadeDuration = 0.25f;
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
		&& !InPostProcessVolume->HasValidPostProcessMaterial(EOutlierPostProcessMaterialType::Stealth)
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
	UpdateStealthView();
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
	UpdateStealthView();
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
	// TickStealth 가 3인칭 스텐실까지 마저 걷어낸다.
	RefreshStealthMeshOverrides();
	UpdateStealthView();
}

void UMaterialPostProcessSubsystem::TickStealth(float DeltaTime)
{
	if (StealthSources.IsEmpty() && StealthMeshRestoreStates.IsEmpty())
	{
		return;
	}

	const float FadeDuration = GetStealthFadeDuration();
	const float FadeSpeed = FadeDuration > 0.0f ? 1.0f / FadeDuration : 0.0f;

	bool bAnyFadeChanged = false;
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
			bAnyFadeChanged = true;
		}

		bAnyStealthActive |= State.bStealthTagActive || State.CurrentFade > 0.0f;
	}

	// 은신이 하나도 안 걸려 있고 되돌릴 것도 없으면 여기서 끝 ( 평상시 비용은 위 루프뿐 ).
	if (!bAnyStealthActive && StealthMeshRestoreStates.IsEmpty())
	{
		return;
	}

	// 대상 집합은 매 틱 다시 모은다. 무기 교체 / 파트너 교체 / 폰 리스폰이 별도 신호 없이 따라온다.
	RefreshStealthMeshOverrides();

	if (bAnyFadeChanged)
	{
		UpdateStealthView();
	}
}

void UMaterialPostProcessSubsystem::RefreshStealthMeshOverrides()
{
	UMaterialInterface* GlassMaterial = GetFirstPersonStealthGlassMaterial();
	const int32 StencilValue = GetStealthStencilValue();

	// 이번에 오버라이드가 유지돼야 하는 메시 집합을 다시 만든다.
	TMap<UMeshComponent*, UMaterialInterface*> DesiredMeshes;
	for (const TPair<TWeakObjectPtr<UOutlierAbilitySystemComponent>, FOutlierStealthSourceState>& Pair
		: StealthSources)
	{
		const UOutlierAbilitySystemComponent* AbilitySystem = Pair.Key.Get();
		const FOutlierStealthSourceState& State = Pair.Value;
		if (!AbilitySystem)
		{
			continue;
		}

		// 1인칭 글래스는 태그가 살아 있는 동안만, 3인칭 스텐실은 페이드가 0 에 닿을 때까지 유지한다.
		const bool bKeepFirstPerson = State.bStealthTagActive;
		const bool bKeepStencil = State.bStealthTagActive || State.CurrentFade > 0.0f;
		if (!bKeepFirstPerson && !bKeepStencil)
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

		if (bKeepFirstPerson && GlassMaterial)
		{
			for (UMeshComponent* Mesh : ScratchFirstPersonMeshes)
			{
				if (Mesh)
				{
					DesiredMeshes.Add(Mesh, GlassMaterial);
				}
			}
		}

		if (bKeepStencil)
		{
			for (UMeshComponent* Mesh : ScratchThirdPersonMeshes)
			{
				if (Mesh)
				{
					DesiredMeshes.Add(Mesh, nullptr);
				}
			}
		}
	}

	// 집합에서 빠진 메시( 교체된 무기, 은신 종료, 파괴된 액터 )를 원상복구한다.
	ScratchStaleMeshes.Reset();
	for (const TPair<TObjectPtr<UMeshComponent>, FOutlierStealthMeshRestoreState>& Pair
		: StealthMeshRestoreStates)
	{
		if (!Pair.Key || !DesiredMeshes.Contains(Pair.Key.Get()))
		{
			ScratchStaleMeshes.Add(Pair.Key);
		}
	}
	for (const TObjectPtr<UMeshComponent>& StaleMesh : ScratchStaleMeshes)
	{
		ClearStealthMeshOverride(StaleMesh);
	}

	for (const TPair<UMeshComponent*, UMaterialInterface*>& Desired : DesiredMeshes)
	{
		ApplyStealthMeshOverride(Desired.Key, Desired.Value, StencilValue);
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
	UMaterialInterface* GlassMaterial,
	int32 StencilValue)
{
	if (!Mesh || StealthMeshRestoreStates.Contains(Mesh))
	{
		return;
	}

	FOutlierStealthMeshRestoreState RestoreState;
	RestoreState.bRenderCustomDepth = Mesh->bRenderCustomDepth;
	RestoreState.CustomDepthStencilValue = Mesh->CustomDepthStencilValue;

	if (GlassMaterial)
	{
		// 1인칭: 표면 머티리얼을 글래스로 교체.
		const int32 MaterialCount = Mesh->GetNumMaterials();
		RestoreState.Materials.Reserve(MaterialCount);
		for (int32 MaterialIndex = 0; MaterialIndex < MaterialCount; ++MaterialIndex)
		{
			RestoreState.Materials.Add(Mesh->GetMaterial(MaterialIndex));
			Mesh->SetMaterial(MaterialIndex, GlassMaterial);
		}
	}
	else
	{
		// 3인칭: 포스트프로세스가 읽을 스텐실만 쓴다.
		Mesh->SetCustomDepthStencilValue(StencilValue);
		Mesh->SetRenderCustomDepth(true);
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

	Mesh->SetCustomDepthStencilValue(RestoreState.CustomDepthStencilValue);
	Mesh->SetRenderCustomDepth(RestoreState.bRenderCustomDepth);
}

void UMaterialPostProcessSubsystem::UpdateStealthView()
{
	if (!BoundPostProcessVolume)
	{
		return;
	}

	// 화면 효과는 로컬 플레이어 것만. 다른 머신의 프록시가 은신해도 내 화면은 건드리지 않는다.
	float LocalFade = 0.0f;
	for (const TPair<TWeakObjectPtr<UOutlierAbilitySystemComponent>, FOutlierStealthSourceState>& Pair
		: StealthSources)
	{
		const UOutlierAbilitySystemComponent* AbilitySystem = Pair.Key.Get();
		if (!AbilitySystem)
		{
			continue;
		}

		const APawn* Avatar = Cast<APawn>(AbilitySystem->GetAvatarActor());
		if (Avatar && Avatar->IsLocallyControlled())
		{
			LocalFade = FMath::Max(LocalFade, Pair.Value.CurrentFade);
		}
	}

	BoundPostProcessVolume->SetPostProcessEnabled(
		EOutlierPostProcessMaterialType::Stealth,
		LocalFade > 0.0f);
	BoundPostProcessVolume->UpdateStealthMaterialParameters(LocalFade);
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

	if (BoundPostProcessVolume)
	{
		BoundPostProcessVolume->SetPostProcessEnabled(EOutlierPostProcessMaterialType::Stealth, false);
		BoundPostProcessVolume->UpdateStealthMaterialParameters(0.0f);
	}
}

float UMaterialPostProcessSubsystem::GetStealthFadeDuration() const
{
	return BoundPostProcessVolume
		? FMath::Max(BoundPostProcessVolume->StealthFadeDuration, 0.0f)
		: DefaultStealthFadeDuration;
}

int32 UMaterialPostProcessSubsystem::GetStealthStencilValue() const
{
	return BoundPostProcessVolume
		? static_cast<int32>(BoundPostProcessVolume->StealthStencilNumber)
		: DefaultStealthStencilValue;
}

UMaterialInterface* UMaterialPostProcessSubsystem::GetFirstPersonStealthGlassMaterial() const
{
	return BoundPostProcessVolume
		? BoundPostProcessVolume->GetFirstPersonStealthGlassMaterial()
		: nullptr;
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
