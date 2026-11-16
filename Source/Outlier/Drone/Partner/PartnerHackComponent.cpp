#include "Drone/Partner/PartnerHackComponent.h"
#include "Abilities/GameplayAbility.h"
#include "Drone/Partner/HackGameplayTags.h"
#include "Drone/Partner/HackableComponent.h"
#include "Drone/Partner/PartnerCharacter.h"
#include "Engine/LocalPlayer.h"
#include "Engine/OverlapResult.h"
#include "Engine/World.h"
#include "Enemy/EnemyBase.h"
#include "UI/ActorBasedWidgetSubsystem.h"
#include "GameplayTags/OutlierGameplayTags.h"
#include "GAS/OutlierAbilitySystemComponent.h"
#include "Upgrade/OutlierUpgradeComponent.h"
#include "Drone/Partner/PartnerPlayerController.h"
#include "FirstPerson/FirstPersonPlayerController.h"
#include "GameFramework/PlayerController.h"
#include "Interface/HackableInterface.h"
#include "UI/HackCandidateLayerWidget.h"
#include "UI/HackCandidateMarkerWidget.h"
#include "UI/HackableInfoWidget.h"
#include "UI/HackMiniGameWidget.h"
#include "UI/LocalPlayerUILayerSubsystem.h"
#include "UI/UILayerGameplayTags.h"
#include "Components/MeshComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SkinnedMeshComponent.h"
#include "Interaction/ActorBodyTargeting.h"
#include "MaterialShared.h"
#include "Materials/Material.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"

const FName UPartnerHackComponent::CursorBodyTag(TEXT("HackCursorBody"));
const FName UPartnerHackComponent::CursorIgnoreTag(TEXT("HackCursorIgnore"));

namespace PartnerHackCursor
{
	constexpr float MinTraceLength = 5000.0f;
}

UPartnerHackComponent::UPartnerHackComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	SetIsReplicatedByDefault(true);
}

void UPartnerHackComponent::BeginPlay()
{
	Super::BeginPlay();
	if (UActorBasedWidgetSubsystem* Subsystem = GetWorld()->GetSubsystem<UActorBasedWidgetSubsystem>())
	{
		Subsystem->RegisterWidgetOwner(this);
	}

	CachedAbilityData.bRequireLineOfSight = bRequireLineOfSight;

	BlockedCandidateTags.AddTag(OutlierGameplayTags::State::Dead());
	BlockedCandidateTags.AddTag(OutlierGameplayTags::State::Locked());
	BlockedCandidateTags.AddTag(OutlierGameplayTags::State::MagneticActive());
	BlockedCandidateTags.AddTag(OutlierGameplayTags::State::Immune());
}

void UPartnerHackComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UActorBasedWidgetSubsystem* Subsystem = GetWorld()->GetSubsystem<UActorBasedWidgetSubsystem>())
	{
		Subsystem->UnregisterWidgetOwner(this);
	}
	if (GetOwner() && GetOwner()->HasAuthority() && IsValid(ActiveHackableComponent))
	{
		FHackResultContext ResultContext;
		ResultContext.RequestId = ActiveHackRequestId;
		ResultContext.TargetActor = ActiveHackableComponent->GetOwner();
		ResultContext.InstigatorActor = PartnerCharacter;
		ResultContext.Result = EHackResult::Cancelled;
		CompleteActiveHack(ResultContext, false);
	}
	else if (GetOwner() && GetOwner()->HasAuthority() && bHackCandidateSearchActive)
	{
		OnHackFinished.Broadcast(EHackResult::Cancelled, false);
	}

	ResetLocalHackHoldProgress();
	ServerHeldHackActor.Reset();
	ServerHoldId = 0;
	ClearActiveHackableComponent();
	DestroyHackMiniGameWidget();
	DestroyCandidateLayerWidget();
	ClearHackCandidates();
	ClearCursorHighlight();
	Super::EndPlay(EndPlayReason);
}

void UPartnerHackComponent::DismissActorBasedWidgets_Implementation()
{
	ResetLocalHackHoldProgress();
	DestroyHackMiniGameWidget();
	DestroyCandidateLayerWidget();
	ClearCursorHighlight();
}

void UPartnerHackComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	// 기존 UI 갱신 경로에서 대여 변경도 확인한다. 빠른 반환으로 Idle 복제가 생략되어도 옛 UI를 닫는다.
	if (ActiveHackableComponent && !IsActiveHackLifetimeCurrent())
	{
		if (GetOwner() && GetOwner()->HasAuthority()) HandleHackTargetUnavailable(ActiveHackableComponent);
		else AbortLocalHackForInvalidTarget();
	}

	if (GetOwner() && GetOwner()->HasAuthority() && ServerHoldId != 0
		&& (!bHackCandidateSearchActive || !PartnerCharacter || !PartnerCharacter->CanAcceptInput()
			|| !IsServerHoldLifetimeCurrent() || !CanMaintainHackHold(ServerHeldHackActor.Get(), BuildQueryContext())))
	{
		const uint32 CancelledHoldId = ServerHoldId;
		ServerHeldHackActor.Reset();
		ServerHoldId = 0;
		ClientCancelHackHold(CancelledHoldId);
	}
	if (!bHackCandidateSearchActive)
	{
		return;
	}

	if (!PartnerCharacter || !PartnerCharacter->IsLocallyControlled())
	{
		return;
	}

	UpdateLocalHackHold();
	RefreshHackCandidates();
	if (!HeldHackActor)
	{
		UpdateCursorHackHover();
	}
}

void UPartnerHackComponent::TryHack_Implementation()
{
	if (!PartnerCharacter || !GetWorld() || !PartnerCharacter->CanAcceptInput())
	{
		if (bDebugHack)
		{
			UE_LOG(LogTemp, Warning, TEXT("[PartnerHackDebug] TryHack blocked Partner=%s World=%d"),
				*GetNameSafe(PartnerCharacter),
				GetWorld() ? 1 : 0);
		}
		return ;
	}

	UOutlierAbilitySystemComponent* AbilitySystem =
		PartnerCharacter->GetOutlierAbilitySystemComponent();
	if (!AbilitySystem || !AbilitySystem->IsPartnerAbilitiesConfigured())
	{
		return;
	}
	if (!IsHackInteractionActive())
	{
		// 취소 직후 늦게 도착한 RPC도 검색을 직접 다시 켜면 안 된다.
		// 실행 중인 해킹 능력이 없으면 GAS 발동 경로로 되돌려 횟수·재시도 제한을 함께 검사한다.
		bool bHackAbilityActive = false;
		for (const FGameplayAbilitySpec& Spec : AbilitySystem->GetActivatableAbilities())
		{
			if (Spec.IsActive() && Spec.Ability
				&& Spec.Ability->GetAssetTags().HasTagExact(OutlierGameplayTags::Ability::Partner::Hacking()))
			{
				bHackAbilityActive = true;
				break;
			}
		}
		if (!bHackAbilityActive)
		{
			AbilitySystem->TryActivatePartnerAbility(OutlierGameplayTags::Ability::Partner::Hacking());
			return;
		}
		if (!AbilitySystem->CanUsePartnerHackCharge())
		{
			return;
		}
	}
	CachedAbilityData.EffectiveRange =
		AbilitySystem->GetPartnerAbilityConfig().HackEffectiveRange;


	if (APartnerPlayerController* PController =
		Cast<APartnerPlayerController>(PartnerCharacter->GetController()))
	{
		const EPartnerPossessionState PossessionState =
			PController->GetPartnerPossessionState();

		if (PossessionState == EPartnerPossessionState::Transitioning)
		{
			UE_LOG(
				LogTemp,
				Error,
				TEXT("Transitioning, %d"),
				static_cast<int32>(PossessionState)
			);
			return;
		}
	}

	//MiniGame;
	if (ActiveHackableComponent || HackMiniGameWidget)
	{
		// 취소 결과를 서버 대상에 먼저 반영한다. Listen Server에서는 Client RPC가 같은 인스턴스의
		// ActiveHackableComponent를 즉시 지울 수 있으므로 UI를 먼저 닫으면 완료 처리가 유실된다.
		CancelActiveHack();
		return ;
	}

	if (bHackCandidateSearchActive) 
	{
		bHackCandidateSearchActive = false;
		ClientStopCandidateSearch();
		DefaultWidgetControl(bHackCandidateSearchActive);
		OnHackFinished.Broadcast(EHackResult::Cancelled, false);
	}
	else
	{
		bHackCandidateSearchActive = true;
		ClientStartCandidateSearch(CachedAbilityData.EffectiveRange);
		DefaultWidgetControl(bHackCandidateSearchActive);
	}

	if (bDebugHack)
	{
		UE_LOG(LogTemp, Warning, TEXT("[PartnerHackDebug] TryHack started Partner=%s Range=%.1f HalfAngle=%.1f LOS=%d Candidates=%d"),
			*GetNameSafe(PartnerCharacter),
			CachedAbilityData.EffectiveRange,
			CandidateHalfAngleDegrees,
			bRequireLineOfSight ? 1 : 0,
			HackCandidateComponents.Num());
	}

	return;
}

void UPartnerHackComponent::EndHackHold()
{
	ResetLocalHackHoldProgress();
}

void UPartnerHackComponent::CancelForReboot()
{
	if (!PartnerCharacter || !PartnerCharacter->HasAuthority())
	{
		return;
	}

	if (IsValid(ActiveHackableComponent))
	{
		FHackResultContext ResultContext;
		ResultContext.RequestId = ActiveHackRequestId;
		ResultContext.TargetActor = ActiveHackableComponent->GetOwner();
		ResultContext.InstigatorActor = PartnerCharacter;
		ResultContext.Result = EHackResult::Cancelled;
		CompleteActiveHack(ResultContext, true);
	}
	else
	{
		const bool bWasSearching = bHackCandidateSearchActive;
		ClientStopHackMiniGame();
		DefaultWidgetControl(false);
		if (bWasSearching)
		{
			OnHackFinished.Broadcast(EHackResult::Cancelled, false);
		}
	}

	ClientStopCandidateSearch();
	StopHackCandidateSearch();
	DestroyHackMiniGameWidget();
}

void UPartnerHackComponent::CacheAbilityData(const FPartnerHackAbilityData& InAbilityData)
{
	CachedAbilityData = InAbilityData;

	bRequireLineOfSight = CachedAbilityData.bRequireLineOfSight;
}

void UPartnerHackComponent::ClientStartCandidateSearch_Implementation(float InEffectiveRange)
{
	if (!PartnerCharacter || !PartnerCharacter->IsLocallyControlled())
	{
		return;
	}

	CachedAbilityData.EffectiveRange = FMath::Max(InEffectiveRange, 0.0f);

	//UE_LOG(LogTemp, Error, TEXT("ClientStartCandidateSearch_Implementation"));

	bHackCandidateSearchActive = true;// 

	EnsureCandidateLayerWidget();
	RefreshHackCandidates();
}

void UPartnerHackComponent::ClientStopCandidateSearch_Implementation()
{
	if (!PartnerCharacter || !PartnerCharacter->IsLocallyControlled())
	{
		return;
	}

	StopHackCandidateSearch();
}

void UPartnerHackComponent::ClientStartHackMiniGame_Implementation(AActor* TargetActor, UHackableComponent* HackableComponent,
	float ClickSuccessRangeMultiplier, float SpinningGapMultiplier, uint32 RequestId,
	int32 GameplayGeneration, int32 LeaseSerial)
{
	if (!PartnerCharacter || !PartnerCharacter->IsLocallyControlled())
	{
		return;
	}

	ResetLocalHackHoldProgress();
	HoveredMarkerWidget = nullptr;
	HoveredHackActor = nullptr;
	bHackCandidateSearchActive = false;

	SetActiveHackableComponent(HackableComponent);
	// 클라이언트 복제보다 RPC가 먼저 도착해도 서버가 시작한 대여 번호를 기준으로 삼는다.
	ActiveHackGeneration = GameplayGeneration;
	ActiveHackLease = LeaseSerial;
	ActiveHackRequestId = RequestId;
	StartHackMiniGame(TargetActor, HackableComponent, ClickSuccessRangeMultiplier, SpinningGapMultiplier);

	ClearHackCandidates();
	LastDebugCandidateCount = INDEX_NONE;

	// If the replacement failed, close the remaining candidate layer normally.
	if (CandidateLayerWidget)
	{
		DestroyCandidateLayerWidget();
	}
}

void UPartnerHackComponent::ClientStopHackMiniGame_Implementation()
{
	if (!PartnerCharacter || !PartnerCharacter->IsLocallyControlled())
	{
		return;
	}

	ClearActiveHackableComponent();
	DestroyHackMiniGameWidget();
}

void UPartnerHackComponent::ClientAbortHackForInvalidTarget_Implementation()
{
	if (!PartnerCharacter || !PartnerCharacter->IsLocallyControlled())
	{
		return;
	}

	AbortLocalHackForInvalidTarget();
}

void UPartnerHackComponent::ServerCompleteHack_Implementation(const FHackResultContext& ResultContext)
{
	CompleteActiveHack(ResultContext, true);
}

void UPartnerHackComponent::CompleteActiveHack(
	const FHackResultContext& ResultContext,
	bool bNotifyClient)
{
	// 사망 취소 뒤 도착한 미니게임 결과는 완료/취소 이벤트를 다시 발생시키지 않는다.
	if (!ActiveHackableComponent || ResultContext.RequestId != ActiveHackRequestId)
	{
		return;
	}
	if (ResultContext.TargetActor != ActiveHackableComponent->GetOwner())
	{
		if (bDebugHack)
		{
			UE_LOG(LogTemp, Warning,
				TEXT("[PartnerHackDebug] Hack failed: stale result Target=%s ActiveTarget=%s"),
				*GetNameSafe(ResultContext.TargetActor),
				*GetNameSafe(ActiveHackableComponent->GetOwner()));
		}
		return;
	}
	if (!IsActiveHackLifetimeCurrent())
	{
		// 이미 반환된 대상에는 Cancelled조차 전달하지 않는다. 새 대여의 빙의 대기를 취소할 수 있다.
		ClearActiveHackableComponent();
		ActiveHackRequestId = 0;
		bHackCompletionPending = false;
		if (bNotifyClient)
		{
			ClientAbortHackForInvalidTarget();
			DefaultWidgetControl(false);
		}
		OnHackFinished.Broadcast(EHackResult::Cancelled, false);
		return;
	}

	if (!IsValid(ActiveHackableComponent)
		|| !IsValid(ActiveHackableComponent->GetOwner())
		|| ActiveHackableComponent->GetOwner()->IsActorBeingDestroyed())
	{
		// EndPlay 중에는 일반 완료 검증을 통과하지 못해도 대상의 대기 결과를 취소해야 한다.
		if (bHackCompletionPending)
		{
			FHackResultContext CancelledContext = ResultContext;
			CancelledContext.InstigatorActor = PartnerCharacter;
			CancelledContext.Result = EHackResult::Cancelled;
			if (IHackableInterface* Handler = Cast<IHackableInterface>(CancelledContext.TargetActor))
			{
				const TWeakObjectPtr<UPartnerHackComponent> WeakThis(this);
				if (Handler->DeferHackCompletion(CancelledContext,
					[WeakThis, bNotifyClient](const FHackResultContext& TerminalContext)
					{
						if (UPartnerHackComponent* Component = WeakThis.Get())
							Component->FinishDeferredHack(TerminalContext, bNotifyClient);
					})) return;
			}
		}
		if (bDebugHack)
		{
			UE_LOG(LogTemp, Warning,
				TEXT("[PartnerHackDebug] Hack failed: active target is no longer valid"));
		}

		ClearActiveHackableComponent();
		bHackCompletionPending = false;
		ActiveHackRequestId = 0;
		if (bNotifyClient)
		{
			ClientAbortHackForInvalidTarget();
			DefaultWidgetControl(false);
		}
		OnHackFinished.Broadcast(EHackResult::Cancelled, false);
		return;
	}

	if (PartnerCharacter
		&& !PartnerCharacter->CanAcceptInput()
		&& ResultContext.Result != EHackResult::Cancelled)
	{
		FHackResultContext CancelledContext = ResultContext;
		CancelledContext.Result = EHackResult::Cancelled;
		CompleteActiveHack(CancelledContext, bNotifyClient);
		return;
	}

	UHackableComponent* CompletedHackableComponent = ActiveHackableComponent;
	AActor* CompletedTargetActor = CompletedHackableComponent->GetOwner();
	const TWeakObjectPtr<AEnemyBase> CompletedPoolEnemy = ActivePoolHackEnemy;
	const int32 CompletedGeneration = ActiveHackGeneration;
	const int32 CompletedLease = ActiveHackLease;
	auto IsCompletedLifetimeCurrent = [&]()
	{
		const AEnemyBase* Enemy = CompletedPoolEnemy.Get();
		return IsValid(CompletedTargetActor) && (!CompletedLease || (Enemy
			&& Enemy->MatchesPoolLease(CompletedGeneration, CompletedLease)
			&& (ResultContext.Result == EHackResult::Cancelled || !Enemy->IsPoolReturnInProgress())));
	};
	FHackResultContext MutableResultContext = ResultContext;
	MutableResultContext.InstigatorActor = PartnerCharacter;
	if (IHackableInterface* Handler = Cast<IHackableInterface>(CompletedTargetActor))
	{
		// 결과 대기 중에도 취소는 대상에 전달한다. 일반 결과의 중복 제출만 무시한다.
		if (bHackCompletionPending && MutableResultContext.Result != EHackResult::Cancelled) return;
		bHackCompletionPending = true;
		const TWeakObjectPtr<UPartnerHackComponent> WeakThis(this);
		if (Handler->DeferHackCompletion(MutableResultContext,
			[WeakThis, bNotifyClient](const FHackResultContext& TerminalContext)
			{
				if (UPartnerHackComponent* Component = WeakThis.Get())
				{
					Component->FinishDeferredHack(TerminalContext, bNotifyClient);
				}
			}))
		{
			return;
		}
		bHackCompletionPending = false;
	}
	if (!IsCompletedLifetimeCurrent() || ActiveHackableComponent != CompletedHackableComponent
		|| ActiveHackRequestId != ResultContext.RequestId) return;
	const bool bPossessionTarget = CompletedHackableComponent->SuccessEffectTags.HasTagExact(
		HackGameplayTags::Effect::Possess());
	ActiveHackRequestId = 0;
	ClearActiveHackableComponent();

	//possess 전, Input widget 정리. -> Enemy Widget이 생긴다면 수정 해야 할 부분.
	if (bNotifyClient)
	{
		ClientStopHackMiniGame();
		DefaultWidgetControl(false);
	}

	MutableResultContext.TargetActor = CompletedTargetActor;

	// 빙의 시작 시 Target 태그는 바뀔 수 있으므로 완료 시에는 차단 상태와 재사용 정책만 재검증한다.
	FHackQueryContext CompletionQuery = BuildQueryContext();
	CompletionQuery.RequiredTags.Reset();
	CompletionQuery.BlockedTags.AddTag(OutlierGameplayTags::State::Dead());
	CompletionQuery.BlockedTags.AddTag(OutlierGameplayTags::State::Locked());
	if (MutableResultContext.Result != EHackResult::Cancelled
		&& !CompletedHackableComponent->CanBeHackTarget(CompletionQuery))
	{
		MutableResultContext.Result = EHackResult::Cancelled;
	}

	if (IHackableInterface* Handler = Cast<IHackableInterface>(CompletedTargetActor))
	{
		Handler->HandleHackCompleted(MutableResultContext);
	}
	// 사용자 콜백이 풀 반환을 수행했다면 성공 이력/효과를 새 개체에 기록하지 않는다.
	if (!IsCompletedLifetimeCurrent()) return;

	if (MutableResultContext.Result == EHackResult::Success)
	{
		CompletedHackableComponent->MarkAsHackedOnce();
	}

	CompletedHackableComponent->CompleteHack(MutableResultContext);
	if (!IsCompletedLifetimeCurrent()) return;
	OnHackFinished.Broadcast(MutableResultContext.Result, bPossessionTarget);
}

void UPartnerHackComponent::FinishDeferredHack(const FHackResultContext& ResultContext, bool bNotifyClient)
{
	if (!bHackCompletionPending || ResultContext.RequestId != ActiveHackRequestId
		|| ResultContext.InstigatorActor != PartnerCharacter
		|| !ActiveHackableComponent || ResultContext.TargetActor != ActiveHackableComponent->GetOwner())
	{
		return;
	}
	if (!IsActiveHackLifetimeCurrent())
	{
		FHackResultContext CancelledContext = ResultContext;
		CancelledContext.Result = EHackResult::Cancelled;
		CompleteActiveHack(CancelledContext, bNotifyClient);
		return;
	}
	const bool bPossessionTarget = ActiveHackableComponent->SuccessEffectTags.HasTagExact(
		HackGameplayTags::Effect::Possess());
	// 대상은 이미 효과를 확정했다. 재진입 전에 완료 권한을 소모하고 UI/어빌리티만 끝낸다.
	bHackCompletionPending = false;
	ActiveHackRequestId = 0;
	ClearActiveHackableComponent();
	if (bNotifyClient)
	{
		ClientStopHackMiniGame();
		DefaultWidgetControl(false);
	}
	OnHackFinished.Broadcast(ResultContext.Result, bPossessionTarget);
}

void UPartnerHackComponent::DefaultWidgetControl_Implementation(bool InFlag)
{
		//UE_LOG(LogTemp, Error, TEXT("DefaultWidgetControl"));

	if (AFirstPersonPlayerController* Controller = Cast< AFirstPersonPlayerController>(PartnerCharacter->GetController()))
	{
		//UE_LOG(LogTemp, Error, TEXT("DefaultWidgetControl Controller Valid"));

		if (InFlag) //Hacking
		{
			Controller->ControlMainWidget(false);
		}
		else
		{
			Controller->ControlMainWidget(true);
		}
	}
}

void UPartnerHackComponent::RefreshHackCandidates()
{
	if (!bHackCandidateSearchActive)
	{
		return;
	}

	if (!PartnerCharacter || !GetWorld())
	{
		if (bDebugHack)
		{
			UE_LOG(LogTemp, Warning, TEXT("[PartnerHackDebug] Refresh blocked Partner=%s World=%d"),
				*GetNameSafe(PartnerCharacter),
				GetWorld() ? 1 : 0);
		}
		ClearHackCandidates();
		return;
	}

	const FHackQueryContext Context = BuildQueryContext();

	TArray<FOverlapResult> OverlapResults;

	FCollisionObjectQueryParams ObjectParams;
	ObjectParams.AddObjectTypesToQuery(ECC_Pawn);
	ObjectParams.AddObjectTypesToQuery(ECC_WorldDynamic);
	ObjectParams.AddObjectTypesToQuery(ECC_PhysicsBody);

	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(PartnerHackCandidateOverlap), false);
	QueryParams.AddIgnoredActor(PartnerCharacter);

	// 탐색 구와 후보 거리 검사는 같은 시점을 기준으로 한다.
	GetWorld()->OverlapMultiByObjectType(
		OverlapResults,
		Context.ViewLocation,
		FQuat::Identity,
		ObjectParams,
		FCollisionShape::MakeSphere(CachedAbilityData.EffectiveRange),
		QueryParams
	);

	TArray<TObjectPtr<UHackableComponent>> NextCandidateComponents;

	for (const FOverlapResult& Result : OverlapResults)
	{
		AActor* Actor = Result.GetActor();
		if (Actor == HeldHackActor) continue;
		UHackableComponent* HackableComponent = ResolveHackableComponent(Actor);
		FVector2D ScreenLocation = FVector2D::ZeroVector;

		if (!IsCandidateActorValid(Actor, HackableComponent, Context, ScreenLocation))
		{
			continue;
		}

		NextCandidateComponents.AddUnique(HackableComponent);

		if (!HackCandidateComponents.Contains(HackableComponent))
		{
			AddHackCandidate(Actor, HackableComponent, ScreenLocation);
		}
	}

	for (int32 Index = HackCandidateComponents.Num() - 1; Index >= 0; --Index)
	{
		if (!NextCandidateComponents.Contains(HackCandidateComponents[Index]))
		{
			RemoveHackCandidateAt(Index);
		}
	}

	if (bDebugHack && LastDebugCandidateCount != HackCandidateComponents.Num())
	{
		LastDebugCandidateCount = HackCandidateComponents.Num();
		UE_LOG(LogTemp, Warning, TEXT("[PartnerHackDebug] Candidate count changed Count=%d Overlaps=%d"),
			HackCandidateComponents.Num(),
			OverlapResults.Num());
	}
}

bool UPartnerHackComponent::TryBeginHackHold()
{
	if (HeldHackActor) return true;
	if (!PartnerCharacter || !PartnerCharacter->IsLocallyControlled()
		|| !PartnerCharacter->CanAcceptInput() || !GetWorld()
		|| !bHackCandidateSearchActive || !IsValid(CandidateLayerWidget)) return false;
	// 커서를 옮긴 프레임에 R을 눌러도 그 순간 가리키는 대상으로 시작하도록 hover를 즉시 다시 판정한다.
	UpdateCursorHackHover();
	if (!IsValid(HoveredMarkerWidget)) return false;

	FVector2D ScreenLocation;
	UHackableComponent* Hackable = ResolveHackableComponent(HoveredHackActor);
	if (!IsCandidateActorValid(HoveredHackActor, Hackable, BuildQueryContext(), ScreenLocation)) return false;

	UHackableInfoWidget* Info = HoveredMarkerWidget->ReleaseInfoForHackHold();
	if (!Info) return false;
	HeldHackActor = HoveredHackActor;
	HackHoldDuration = HoveredMarkerWidget->GetHackHoldDuration(CachedAbilityData.MiniGameTime);
	HackHoldStartTime = GetWorld()->GetTimeSeconds();
	if (++LocalHoldId == 0) ++LocalHoldId;
	CandidateLayerWidget->BeginHackHold(HeldHackActor, Info);
	HoveredHackActor = nullptr;
	HoveredMarkerWidget = nullptr;
	MarkerHoveredActor = nullptr;
	MarkerHoveredWidget = nullptr;
	// Held info belongs to the layer, independently of candidate overlap and marker lifetime.
	for (int32 Index = HackCandidateActors.Num() - 1; Index >= 0; --Index)
	{
		if (HackCandidateActors[Index] == HeldHackActor) RemoveHackCandidateAt(Index);
	}
	// 홀드 중에는 hover 대신 홀드 대상에 하이라이트를 유지한다.
	RefreshCursorHighlight();
	ServerBeginHackHold(HeldHackActor, LocalHoldId);
	return true;
}

void UPartnerHackComponent::NotifyHackMarkerHovered(UHackCandidateMarkerWidget* MarkerWidget, AActor* TargetActor)
{
	if (!MarkerWidget || !TargetActor || HeldHackActor) return;
	MarkerHoveredWidget = MarkerWidget;
	MarkerHoveredActor = TargetActor;
	UpdateCursorHackHover();
}

void UPartnerHackComponent::NotifyHackMarkerUnhovered(UHackCandidateMarkerWidget* MarkerWidget, AActor* TargetActor, bool bMarkerDestroying)
{
	if (MarkerHoveredWidget == MarkerWidget)
	{
		MarkerHoveredWidget = nullptr;
		MarkerHoveredActor = nullptr;
	}
	if (HoveredMarkerWidget != MarkerWidget) return;
	if (bMarkerDestroying || HeldHackActor)
	{
		// 파괴 중인 마커는 다시 찾지 않는다. 정보창은 마커가 함께 정리한다.
		HoveredMarkerWidget = nullptr;
		HoveredHackActor = nullptr;
		RefreshCursorHighlight();
		return;
	}
	// 마커에서 같은 대상의 몸체로 커서가 넘어가도 정보창이 깜빡이지 않도록 바로 다시 판정한다.
	UpdateCursorHackHover();
}

void UPartnerHackComponent::UpdateCursorHackHover()
{
	if (!bHackCandidateSearchActive || HeldHackActor || !PartnerCharacter || !PartnerCharacter->IsLocallyControlled())
	{
		SetHoveredHackTarget(nullptr);
		return;
	}
	AActor* Target = ResolveCursorHackTarget();
	if (!Target && IsValid(MarkerHoveredWidget) && HackCandidateActors.Contains(MarkerHoveredActor.Get()))
	{
		Target = MarkerHoveredActor;
	}
	SetHoveredHackTarget(Target);
}

void UPartnerHackComponent::SetHoveredHackTarget(AActor* TargetActor)
{
	UHackCandidateMarkerWidget* Marker = TargetActor && IsValid(CandidateLayerWidget)
		? CandidateLayerWidget->FindMarker(TargetActor) : nullptr;
	// 투영되지 않아 숨긴 마커는 커서 선택에서도 제외한다.
	if (!IsValid(Marker) || Marker->GetVisibility() == ESlateVisibility::Collapsed)
	{
		Marker = nullptr;
		TargetActor = nullptr;
	}
	if (TargetActor != HoveredHackActor || Marker != HoveredMarkerWidget)
	{
		if (IsValid(HoveredMarkerWidget))
		{
			HoveredMarkerWidget->SetInfoShown(false);
		}
		HoveredHackActor = TargetActor;
		HoveredMarkerWidget = Marker;
		if (Marker)
		{
			Marker->SetInfoShown(true);
		}
	}
	RefreshCursorHighlight();
}

AActor* UPartnerHackComponent::ResolveCursorHackTarget() const
{
	if (!PartnerCharacter || HackCandidateActors.IsEmpty()) return nullptr;
	const APlayerController* PC = Cast<APlayerController>(PartnerCharacter->GetController());
	FVector Start;
	FVector Direction;
	if (!PC || !PC->IsLocalController() || !PC->DeprojectMousePositionToWorld(Start, Direction)) return nullptr;
	return ResolveCursorHackTargetAlongRay(Start, Direction);
}

AActor* UPartnerHackComponent::ResolveCursorHackTargetAlongRay(const FVector& Start, const FVector& Direction) const
{
	if (HackCandidateActors.IsEmpty() || Direction.IsNearlyZero()) return nullptr;
	const float TraceLength = FMath::Max(CachedAbilityData.EffectiveRange * 2.0f, PartnerHackCursor::MinTraceLength);
	const FVector End = Start + Direction * TraceLength;

	AActor* BestTarget = nullptr;
	float BestDistance = TNumericLimits<float>::Max();
	bool bBestPrecise = false;
	TArray<UPrimitiveComponent*> Bodies;
	for (AActor* Candidate : HackCandidateActors)
	{
		if (!IsValid(Candidate) || Candidate->IsHidden())
		{
			continue;
		}
		Bodies.Reset();
		GatherCursorBodyComponents(Candidate, Bodies);
		for (UPrimitiveComponent* Body : Bodies)
		{
			ActorBodyTargeting::FBodyHit Hit;
			FCollisionQueryParams Params(SCENE_QUERY_STAT(PartnerHackCursorBody), true);
			if (!ActorBodyTargeting::IntersectBody(*Body, Start, End, Params, Hit))
			{
				continue;
			}
			// 해킹의 기존 거리 계산과 동률 선착순을 유지한다. 차폐를 통과한 후보만 갱신한다.
			const float Distance = Hit.bPrecise ? FVector::Dist(Start, Hit.Location) : Hit.Time * TraceLength;
			if (ActorBodyTargeting::IsBetterCandidate(Hit.bPrecise, Distance, bBestPrecise, BestDistance)
				&& HasClearCursorPath(Start, Hit.Location))
			{
				BestDistance = Distance;
				BestTarget = Candidate;
				bBestPrecise = Hit.bPrecise;
			}
		}
	}
	// 앞쪽 후보의 넉넉한 경계 상자가 뒤쪽 후보의 정확한 몸체 판정을 가로채지 않도록 정밀 판정을 우선한다.
	return BestTarget;
}

bool UPartnerHackComponent::HasClearCursorPath(const FVector& Start, const FVector& End) const
{
	UWorld* World = GetWorld();
	if (!World) return false;

	FCollisionQueryParams Params(SCENE_QUERY_STAT(PartnerHackCursorBlock), false);
	TArray<AActor*> ChildActors;
	if (PartnerCharacter)
	{
		Params.AddIgnoredActor(PartnerCharacter);
		PartnerCharacter->GetAllChildActors(ChildActors);
		Params.AddIgnoredActors(ChildActors);
	}
	// 후보 자신의 캡슐·조명 등이 몸체 앞에서 벽으로 잡히지 않도록 후보와 그 자식 액터는 벽 판정에서 뺀다.
	for (AActor* Candidate : HackCandidateActors)
	{
		if (!IsValid(Candidate)) continue;
		Params.AddIgnoredActor(Candidate);
		ChildActors.Reset();
		Candidate->GetAllChildActors(ChildActors);
		Params.AddIgnoredActors(ChildActors);
	}

	// 횟수 제한 없이 후보 교차 지점까지 확인한다. 반복마다 맞은 제외 대상을
	// 무시 목록에 추가하므로, 실제 장애물 또는 이 구간의 무충돌 결과에서 끝난다.
	while (true)
	{
		FHitResult Hit;
		if (!World->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, Params))
		{
			// 후보 교차 지점까지 실제로 검사를 끝낸 경우에만 선택을 허용한다.
			return true;
		}
		if (AActor* HitActor = Hit.GetActor(); IsCursorIgnoredActor(HitActor))
		{
			// 클래스 제외는 액터 전체에 적용한다.
			Params.AddIgnoredActor(HitActor);
		}
		else if (UPrimitiveComponent* HitComponent = Hit.GetComponent();
			HitComponent && HitComponent->ComponentHasTag(CursorIgnoreTag))
		{
			// 컴포넌트 태그는 같은 액터의 다른 차폐 메시까지 제외하지 않는다.
			Params.AddIgnoredComponent(HitComponent);
		}
		else
		{
			return false;
		}
	}
}

bool UPartnerHackComponent::IsCursorIgnoredActor(const AActor* HitActor) const
{
	if (!HitActor) return false;
	for (const TSubclassOf<AActor>& IgnoredClass : CursorIgnoredActorClasses)
	{
		if (IgnoredClass && HitActor->IsA(IgnoredClass)) return true;
	}
	return false;
}

void UPartnerHackComponent::GatherCursorBodyComponents(AActor* Actor, TArray<UPrimitiveComponent*>& OutBodies)
{
	if (!IsValid(Actor)) return;
	// 액터가 직접 소유한 컴포넌트만 본다. ChildActor(조명 등)의 컴포넌트는 포함되지 않는다.
	TInlineComponentArray<UPrimitiveComponent*> Primitives(Actor);
	for (UPrimitiveComponent* Primitive : Primitives)
	{
		if (IsValid(Primitive) && Primitive->IsRegistered()
			&& !Primitive->ComponentHasTag(CursorIgnoreTag)
			&& Primitive->ComponentHasTag(CursorBodyTag))
		{
			OutBodies.Add(Primitive);
		}
	}
	if (!OutBodies.IsEmpty()) return;
	for (UPrimitiveComponent* Primitive : Primitives)
	{
		if (ActorBodyTargeting::IsVisibleMesh(Primitive))
		{
			OutBodies.Add(Primitive);
		}
	}
}

void UPartnerHackComponent::RefreshCursorHighlight()
{
	AActor* Desired = nullptr;
	if (bHackCandidateSearchActive)
	{
		Desired = HeldHackActor ? HeldHackActor.Get() : HoveredHackActor.Get();
	}
	if (Desired == HighlightedActor.Get()) return;
	ClearCursorHighlight();
	ApplyCursorHighlight(Desired);
}

void UPartnerHackComponent::ApplyCursorHighlight(AActor* TargetActor)
{
	if (!IsValid(TargetActor)) return;
	HighlightedActor = TargetActor;
	UMaterialInterface* HighlightMaterial = HackTrackingHighlightMaterial;
	if (!HighlightMaterial) return;

	// 스켈레탈 메시에는 해당 용도로 컴파일된 머티리얼만 적용한다.
	const UMaterial* BaseMaterial = HighlightMaterial->GetMaterial();
	const bool bSkeletalUsable = BaseMaterial
		&& (BaseMaterial->bUsedAsSpecialEngineMaterial || BaseMaterial->GetUsageByFlag(MATUSAGE_SkeletalMesh));

	// 머티리얼의 Time 노드(게임 시간)와 같은 기준으로 hover 시작 시각을 넣는다. 파라미터가 없는 머티리얼엔 영향 없음.
	if (!HighlightMID || HighlightMID->Parent != HighlightMaterial)
	{
		HighlightMID = UMaterialInstanceDynamic::Create(HighlightMaterial, this);
	}
	UMaterialInterface* OverlayMaterial = HighlightMaterial;
	if (HighlightMID)
	{
		static const FName HoverStartTimeParam(TEXT("HoverStartTime"));
		HighlightMID->SetScalarParameterValue(HoverStartTimeParam, GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f);
		OverlayMaterial = HighlightMID;
	}

	TInlineComponentArray<UMeshComponent*> Meshes(TargetActor);
	for (UMeshComponent* Mesh : Meshes)
	{
		if (!ActorBodyTargeting::IsVisibleMesh(Mesh))
		{
			continue;
		}
		if (Mesh->IsA<USkinnedMeshComponent>() && !bSkeletalUsable)
		{
			if (!bLoggedHighlightSkeletalUsage)
			{
				bLoggedHighlightSkeletalUsage = true;
				UE_LOG(LogTemp, Warning,
					TEXT("[PartnerHack] Skipping highlight on skeletal meshes: enable Used with Skeletal Mesh on %s"),
					*HighlightMaterial->GetPathName());
			}
			continue;
		}
		FPartnerHackCursorHighlightState& State = HighlightedMeshes.AddDefaulted_GetRef();
		State.Mesh = Mesh;
		State.OriginalMaterial = Mesh->GetOverlayMaterial();
		Mesh->SetOverlayMaterial(OverlayMaterial);
	}
}

void UPartnerHackComponent::ClearCursorHighlight()
{
	for (const FPartnerHackCursorHighlightState& State : HighlightedMeshes)
	{
		if (UMeshComponent* Mesh = State.Mesh.Get())
		{
			Mesh->SetOverlayMaterial(State.OriginalMaterial.Get());
		}
	}
	HighlightedMeshes.Reset();
	HighlightedActor.Reset();
}

bool UPartnerHackComponent::CanMaintainHackHold(AActor* Actor, const FHackQueryContext& Context) const
{
	UHackableComponent* Hackable = ResolveHackableComponent(Actor);
	return Hackable && Hackable->IsHackTargetType() && Hackable->CanBeHackTarget(Context)
		&& FVector::DistSquared(Context.ViewLocation, IHackableInterface::ResolveHackTargetLocation(Actor))
			<= FMath::Square(FMath::Max(Context.MaxRange, 0.0f));
}

void UPartnerHackComponent::UpdateLocalHackHold()
{
	if (!HeldHackActor) return;
	if (!PartnerCharacter || !PartnerCharacter->CanAcceptInput() || !GetWorld()
		|| !bHackCandidateSearchActive || !IsValid(CandidateLayerWidget)
		|| !CanMaintainHackHold(HeldHackActor, BuildQueryContext()))
	{
		ResetLocalHackHoldProgress();
		return;
	}
	const float Progress = FMath::Clamp((GetWorld()->GetTimeSeconds() - HackHoldStartTime) / HackHoldDuration, 0.0f, 1.0f);
	CandidateLayerWidget->SetHackHoldProgress(Progress);
	if (Progress >= 1.0f)
	{
		AActor* Target = HeldHackActor;
		const uint32 CompletedHoldId = LocalHoldId;
		ResetLocalHackHoldProgress(false);
		ServerTryStartHack(Target, CompletedHoldId);
	}
}

void UPartnerHackComponent::ResetLocalHackHoldProgress(bool bNotifyServer)
{
	const bool bWasHolding = HeldHackActor != nullptr;
	HeldHackActor = nullptr;
	HackHoldStartTime = 0.0f;
	HackHoldDuration = 0.0f;
	if (CandidateLayerWidget) CandidateLayerWidget->EndHackHold();
	if (bWasHolding && bNotifyServer) ServerEndHackHold(LocalHoldId);
	RefreshCursorHighlight();
}

void UPartnerHackComponent::ServerBeginHackHold_Implementation(AActor* TargetActor, uint32 HoldId)
{
	ServerHeldHackActor.Reset();
	ServerHoldId = 0;
	const UOutlierAbilitySystemComponent* ASC = PartnerCharacter ? PartnerCharacter->GetOutlierAbilitySystemComponent() : nullptr;
	FVector2D ScreenLocation;
	if (!HoldId || !bHackCandidateSearchActive || !PartnerCharacter || !PartnerCharacter->CanAcceptInput()
		|| !ASC || !ASC->CanUsePartnerHackCharge()
		|| !IsCandidateActorValid(TargetActor, ResolveHackableComponent(TargetActor), BuildQueryContext(), ScreenLocation))
	{
		ClientCancelHackHold(HoldId);
		return;
	}
	ServerHeldHackActor = TargetActor;
	ServerHoldId = HoldId;
	ServerHeldPoolEnemy = Cast<AEnemyBase>(TargetActor);
	if (AEnemyBase* Enemy = ServerHeldPoolEnemy.Get(); Enemy && Enemy->IsPoolManaged())
	{
		ServerHoldGeneration = Enemy->GetPoolGameplayGeneration();
		ServerHoldLease = Enemy->GetPoolLeaseSerial();
	}
	else
	{
		ServerHeldPoolEnemy.Reset();
		ServerHoldGeneration = ServerHoldLease = 0;
	}
}

void UPartnerHackComponent::ServerEndHackHold_Implementation(uint32 HoldId)
{
	if (ServerHoldId != HoldId) return;
	ServerHeldHackActor.Reset();
	ServerHoldId = 0;
}

void UPartnerHackComponent::ClientCancelHackHold_Implementation(uint32 HoldId)
{
	if (LocalHoldId == HoldId) ResetLocalHackHoldProgress(false);
}

void UPartnerHackComponent::ServerTryStartHack_Implementation(AActor* TargetActor, uint32 HoldId)
{
	if (ActiveHackableComponent || bHackCompletionPending) return;
	// Only a target accepted when R was pressed may bypass viewport/angle/LOS checks.
	if (!HoldId || HoldId != ServerHoldId || TargetActor != ServerHeldHackActor.Get()) return;
	if (!IsServerHoldLifetimeCurrent())
	{
		ServerHeldHackActor.Reset();
		ServerHoldId = 0;
		ClientCancelHackHold(HoldId);
		return;
	}
	ServerHeldHackActor.Reset();
	ServerHoldId = 0;
	if (!PartnerCharacter || !PartnerCharacter->CanAcceptInput()) return;
	const UOutlierAbilitySystemComponent* AbilitySystem = PartnerCharacter->GetOutlierAbilitySystemComponent();
	// 취소 전에 시작한 홀드의 완료 RPC가 뒤늦게 도착해도 새 해킹을 시작하지 않는다.
	if (!bHackCandidateSearchActive || !AbilitySystem || !AbilitySystem->CanUsePartnerHackCharge())
	{
		return;
	}

	UHackableComponent* HackableComponent = ResolveHackableComponent(TargetActor);
	if (!HackableComponent)
	{
		if (bDebugHack)
		{
			UE_LOG(LogTemp, Warning, TEXT("[PartnerHackDebug] TryStartHack failed: no hackable component Target=%s"),
				*GetNameSafe(TargetActor));
		}
		return;
	}

	FVector2D ScreenLocation = FVector2D::ZeroVector;
	FHackQueryContext Context = BuildQueryContext();
	// Locked holds use range and target state, even behind the camera or cover.
	if (!CanMaintainHackHold(TargetActor, Context))
	{
		if (bDebugHack)
		{
			UE_LOG(LogTemp, Warning, TEXT("[PartnerHackDebug] TryStartHack failed: invalid candidate Target=%s Hackable=%s"),
				*GetNameSafe(TargetActor),
				*GetNameSafe(HackableComponent));
		}
		return;
	}

	DeactivateUnselectedCandidates(HackableComponent);
	bHackCandidateSearchActive = false;
	SetActiveHackableComponent(HackableComponent);
	if (++NextHackRequestId == 0) ++NextHackRequestId;
	ActiveHackRequestId = NextHackRequestId;
	Context.RequestId = ActiveHackRequestId;

	//Actor Override
	if (IHackableInterface* Handler = Cast<IHackableInterface>(TargetActor))
	{
		Handler->HandleHackStarted(Context);
	}
	// 시작 콜백에서 사망/취소가 발생했다면 같은 호출 스택에서 완료를 다시 제출하지 않는다.
	if (ActiveHackableComponent != HackableComponent || ActiveHackRequestId != Context.RequestId) return;

	if (!HackableComponent->RequiresMiniGame())
	{
		ClientStopCandidateSearch();

		FHackResultContext ResultContext;
		ResultContext.RequestId = Context.RequestId;
		ResultContext.TargetActor = TargetActor;
		ResultContext.InstigatorActor = PartnerCharacter;
		ResultContext.Result = EHackResult::Success;
		CompleteActiveHack(ResultContext, true);

		if (bDebugHack)
		{
			UE_LOG(LogTemp, Warning,
				TEXT("[PartnerHackDebug] Hack completed without minigame Target=%s Hackable=%s"),
				*GetNameSafe(TargetActor),
				*GetNameSafe(HackableComponent));
		}
		return;
	}

	// 시작 시 서버에서 고정한 배율을 전달한다. 클라이언트 Config/태그 복제 순서에 의존하지 않는다.
	ClientStartHackMiniGame(TargetActor, HackableComponent,
		ResolveMiniGameSuccessRangeMultiplier(false), ResolveMiniGameSuccessRangeMultiplier(true), Context.RequestId,
		ActiveHackGeneration, ActiveHackLease);

	if (bDebugHack)
	{
		UE_LOG(LogTemp, Warning, TEXT("[PartnerHackDebug] TryStartHack success Target=%s Hackable=%s Screen=(%.1f, %.1f)"),
			*GetNameSafe(TargetActor),
			*GetNameSafe(HackableComponent),
			ScreenLocation.X,
			ScreenLocation.Y);
	}
}

void UPartnerHackComponent::ClientCompleteHack()
{
	
}

void UPartnerHackComponent::ClearHackCandidates()
{
	for (int32 Index = HackCandidateComponents.Num() - 1; Index >= 0; --Index)
	{
		RemoveHackCandidateAt(Index);
	}
}

void UPartnerHackComponent::StopHackCandidateSearch()
{
	ResetLocalHackHoldProgress();
	ServerHeldHackActor.Reset();
	ServerHoldId = 0;
	HoveredMarkerWidget = nullptr;
	HoveredHackActor = nullptr;
	MarkerHoveredWidget = nullptr;
	MarkerHoveredActor = nullptr;

	bHackCandidateSearchActive = false;
	ClearCursorHighlight();
	DestroyCandidateLayerWidget();
	ClearHackCandidates();
	LastDebugCandidateCount = INDEX_NONE;

	if (bDebugHack)
	{
		UE_LOG(LogTemp, Warning, TEXT("[PartnerHackDebug] Candidate search stopped"));
	}
}

FHackQueryContext UPartnerHackComponent::BuildQueryContext() const
{
	FHackQueryContext Context;
	Context.InstigatorActor = PartnerCharacter;
	Context.MaxRange = CachedAbilityData.EffectiveRange;
	Context.RequiredTags = RequiredCandidateTags;
	Context.BlockedTags = BlockedCandidateTags;
	Context.HackMultiUseTags.AddTag(HackGameplayTags::Use::Multiple());

	if (PartnerCharacter)
	{
		if (AController* Controller = PartnerCharacter->GetController())
		{
			FRotator ViewRotation;
			Controller->GetPlayerViewPoint(Context.ViewLocation, ViewRotation);
			Context.ViewDirection = ViewRotation.Vector();
		}
		else
		{
			Context.ViewLocation = PartnerCharacter->GetActorLocation();
			Context.ViewDirection = PartnerCharacter->GetActorForwardVector();
		}
	}

	return Context;
}

UHackableComponent* UPartnerHackComponent::ResolveHackableComponent(AActor* Actor) const
{
	if (!IsValid(Actor) || Actor->IsActorBeingDestroyed() || Actor == PartnerCharacter)
	{
		return nullptr;
	}

	if (!Actor->GetClass()->ImplementsInterface(UHackableInterface::StaticClass()))
	{
		return nullptr;
	}

	UHackableComponent* HackableComponent = Cast<IHackableInterface>(Actor)->GetHackableComponent();
	if (bDebugHack && !HackableComponent)
	{
		UE_LOG(LogTemp, Warning, TEXT("[PartnerHackDebug] Interface actor returned null component Actor=%s Class=%s"),
			*GetNameSafe(Actor),
			*GetNameSafe(Actor->GetClass()));
	}

	return HackableComponent;
}

bool UPartnerHackComponent::IsCandidateActorValid(AActor* Actor, UHackableComponent* HackableComponent, const FHackQueryContext& Context, FVector2D& OutScreenLocation) const
{
	if (!IsValid(Actor)
		|| Actor->IsActorBeingDestroyed()
		|| !IsValid(HackableComponent)
		|| !HackableComponent->IsHackTargetType())
	{
		return false;
	}

	if (!IsInsidePartnerFrustum(Actor, OutScreenLocation))
	{
		return false;
	}

	if (!HackableComponent->CanBeHackTarget(Context))
	{
		return false;
	}

	if (bRequireLineOfSight && !HasLineOfSight(Actor))
	{
		return false;
	}

	return true;
}

bool UPartnerHackComponent::IsInsidePartnerFrustum(AActor* Actor, FVector2D& OutScreenLocation) const
{
	if (!Actor || !PartnerCharacter)
	{
		return false;
	}

	const FHackQueryContext Context = BuildQueryContext();
	const FVector TargetLocation = IHackableInterface::ResolveHackTargetLocation(Actor);
	const FVector ToTarget = TargetLocation - Context.ViewLocation;
	const float DistanceSq = ToTarget.SizeSquared();

	if (DistanceSq > FMath::Square(CachedAbilityData.EffectiveRange))
	{
		return false;
	}

	const FVector TargetDirection = ToTarget.GetSafeNormal();
	const float Dot = FVector::DotProduct(Context.ViewDirection.GetSafeNormal(), TargetDirection);
	const float MinDot = FMath::Cos(FMath::DegreesToRadians(CandidateHalfAngleDegrees));
	if (Dot < MinDot)
	{
		return false;
	}

	APlayerController* PlayerController = Cast<APlayerController>(PartnerCharacter->GetController());
	if (!PlayerController || !PlayerController->IsLocalController())
	{
		OutScreenLocation = FVector2D::ZeroVector;
		return true;
	}

	if (!PlayerController->ProjectWorldLocationToScreen(TargetLocation, OutScreenLocation, true))
	{
		return false;
	}

	int32 ViewportX = 0;
	int32 ViewportY = 0;
	PlayerController->GetViewportSize(ViewportX, ViewportY);

	return ViewportX <= 0 || ViewportY <= 0
		|| (OutScreenLocation.X >= 0.0f
			&& OutScreenLocation.Y >= 0.0f
			&& OutScreenLocation.X <= ViewportX
			&& OutScreenLocation.Y <= ViewportY);
}

bool UPartnerHackComponent::HasLineOfSight(AActor* Actor) const
{
	if (!Actor || !PartnerCharacter || !GetWorld())
	{
		return false;
	}

	FHackQueryContext Context = BuildQueryContext();

	FHitResult Hit;
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(PartnerHackLineOfSight), false);
	QueryParams.AddIgnoredActor(PartnerCharacter);

	const bool bHit = GetWorld()->LineTraceSingleByChannel(
		Hit,
		Context.ViewLocation,
		IHackableInterface::ResolveHackTargetLocation(Actor),
		ECC_Visibility,
		QueryParams
	);

	return !bHit || Hit.GetActor() == Actor;
}

void UPartnerHackComponent::EnsureCandidateLayerWidget()
{
	if (CandidateLayerWidget || !PartnerCharacter)
	{
		return;
	}

	APlayerController* PlayerController = Cast<APlayerController>(PartnerCharacter->GetController());
	if (!PlayerController || !PlayerController->IsLocalController())
	{
		UE_LOG(LogTemp, Error, TEXT("Not Local"));
		return;
	}

	TSubclassOf<UHackCandidateLayerWidget> EffectiveLayerClass = CandidateLayerWidgetClass;
	if (!EffectiveLayerClass)
	{
		EffectiveLayerClass = UHackCandidateLayerWidget::StaticClass();
	}

	CandidateLayerWidget = CreateWidget<UHackCandidateLayerWidget>(PlayerController, EffectiveLayerClass);
	if (!CandidateLayerWidget)
	{
		return;
	}

	CandidateLayerWidget->SetMarkerWidgetClass(CandidateMarkerWidgetClass);
	CandidateLayerWidget->SetHackableInfoWidgetClass(HackableInfoWidgetClass);
	CandidateLayerWidget->BindHackComponent(this);

	if (ULocalPlayerUILayerSubsystem* LayerSubsystem = GetUILayerSubsystem())
	{
		HackCandidateLayerHandle = LayerSubsystem->PushWidget(
			UILayerTags::Gameplay(),
			CandidateLayerWidget,
			FirstPersonInputModeTags::Hack(),
			this,
			EUILayerFocusTarget::Widget,
			true);
	}

	if (!HackCandidateLayerHandle.IsValid())
	{
		UE_LOG(LogTemp, Error,
			TEXT("[PartnerHack] Failed to push candidate widget into the local UI layer"));
		CandidateLayerWidget->BindHackComponent(nullptr);
		CandidateLayerWidget = nullptr;
	}
}

void UPartnerHackComponent::DestroyCandidateLayerWidget()
{
	if (!CandidateLayerWidget)
	{
		if (ULocalPlayerUILayerSubsystem* LayerSubsystem = GetUILayerSubsystem())
		{
			LayerSubsystem->PopLayer(HackCandidateLayerHandle);
		}
		HackCandidateLayerHandle.Reset();
		return;
	}

	CandidateLayerWidget->BindHackComponent(nullptr);

	bool bPoppedLayer = false;
	if (ULocalPlayerUILayerSubsystem* LayerSubsystem = GetUILayerSubsystem())
	{
		bPoppedLayer = LayerSubsystem->PopLayer(HackCandidateLayerHandle);
	}
	if (!bPoppedLayer)
	{
		CandidateLayerWidget->RemoveFromParent();
	}
	HackCandidateLayerHandle.Reset();
	CandidateLayerWidget = nullptr;
}

float UPartnerHackComponent::ResolveMiniGameSuccessRangeMultiplier(bool bSpinningCircle) const
{
	if (!PartnerCharacter || !PartnerCharacter->HasAuthority()) return 1.0f;
	const UOutlierUpgradeComponent* Upgrade = PartnerCharacter->GetUpgradeComponent();
	const UOutlierAbilitySystemComponent* ASC = PartnerCharacter->GetOutlierAbilitySystemComponent();
	if (!Upgrade || !Upgrade->HasUpgradeTag(OutlierGameplayTags::Upgrade::Partner::Hacking::MinigameEase())
		|| !ASC || !ASC->IsPartnerAbilitiesConfigured())
	{
		return 1.0f;
	}
	const FOutlierPartnerAbilityConfig& Config = ASC->GetPartnerAbilityConfig();
	return bSpinningCircle ? Config.HackSpinningGapMultiplier : Config.HackClickSuccessRangeMultiplier;
}

bool UPartnerHackComponent::EnsureHackMiniGameWidget(AActor* TargetActor, UHackableComponent* HackableComponent,
	float ClickSuccessRangeMultiplier, float SpinningGapMultiplier)
{
	if (!PartnerCharacter || !TargetActor || !HackableComponent)
	{
		return false;
	}

	if (HackMiniGameWidget)
	{
		return true;
	}

	APlayerController* PlayerController = Cast<APlayerController>(PartnerCharacter->GetController());
	if (!PlayerController || !PlayerController->IsLocalController())
	{
		return false;
	}

	TSubclassOf<UHackMiniGameWidget> EffectiveWidgetClass = HackMiniGameWidgetClass;
	if (!EffectiveWidgetClass)
	{
		EffectiveWidgetClass = UHackMiniGameWidget::StaticClass();
	}

	HackMiniGameWidget = CreateWidget<UHackMiniGameWidget>(PlayerController, EffectiveWidgetClass);
	if (!HackMiniGameWidget)
	{
		return false;
	}

	HackMiniGameWidget->InitializeHackMiniGame(TargetActor, HackableComponent, this,
		ClickSuccessRangeMultiplier, SpinningGapMultiplier);
	HackMiniGameWidget->SetHackRequestId(ActiveHackRequestId);
	HackMiniGameWidget->SetMiniGameTimeLimit(CachedAbilityData.MiniGameTime);
	HackMiniGameWidget->OnHackMiniGameFinished.AddDynamic(this, &UPartnerHackComponent::HandleHackMiniGameFinished);

	if (ULocalPlayerUILayerSubsystem* LayerSubsystem = GetUILayerSubsystem())
	{
		if (HackCandidateLayerHandle.IsValid())
		{
			HackMiniGameLayerHandle = LayerSubsystem->ReplaceWidget(
				HackCandidateLayerHandle,
				UILayerTags::Gameplay(),
				HackMiniGameWidget,
				FirstPersonInputModeTags::Hack(),
				this,
				EUILayerFocusTarget::Widget,
				true);
		}
		else
		{
			HackMiniGameLayerHandle = LayerSubsystem->PushWidget(
				UILayerTags::Gameplay(),
				HackMiniGameWidget,
				FirstPersonInputModeTags::Hack(),
				this,
				EUILayerFocusTarget::Widget,
				true);
		}
	}

	if (!HackMiniGameLayerHandle.IsValid())
	{
		UE_LOG(LogTemp, Error,
			TEXT("[PartnerHack] Failed to push mini-game widget into the local UI layer"));
		HackMiniGameWidget->OnHackMiniGameFinished.RemoveDynamic(
			this,
			&UPartnerHackComponent::HandleHackMiniGameFinished);
		HackMiniGameWidget = nullptr;
		return false;
	}

	if (CandidateLayerWidget)
	{
		CandidateLayerWidget->BindHackComponent(nullptr);
		CandidateLayerWidget = nullptr;
	}
	HackCandidateLayerHandle.Reset();

	return HackMiniGameWidget->StartHacking();
}

void UPartnerHackComponent::DestroyHackMiniGameWidget()
{

	if (!HackMiniGameWidget)
	{
		if (ULocalPlayerUILayerSubsystem* LayerSubsystem = GetUILayerSubsystem())
		{
			LayerSubsystem->PopLayer(HackMiniGameLayerHandle);
		}
		HackMiniGameLayerHandle.Reset();
		return;
	}

	HackMiniGameWidget->OnHackMiniGameFinished.RemoveDynamic(this, &UPartnerHackComponent::HandleHackMiniGameFinished);

	bool bPoppedLayer = false;
	if (ULocalPlayerUILayerSubsystem* LayerSubsystem = GetUILayerSubsystem())
	{
		bPoppedLayer = LayerSubsystem->PopLayer(HackMiniGameLayerHandle);
	}
	if (!bPoppedLayer)
	{
		HackMiniGameWidget->RemoveFromParent();
	}
	HackMiniGameLayerHandle.Reset();
	HackMiniGameWidget = nullptr;
}

ULocalPlayerUILayerSubsystem* UPartnerHackComponent::GetUILayerSubsystem() const
{
	if (!PartnerCharacter)
	{
		return nullptr;
	}

	APlayerController* PlayerController =
		Cast<APlayerController>(PartnerCharacter->GetController());
	if (!PlayerController || !PlayerController->IsLocalController())
	{
		return nullptr;
	}

	ULocalPlayer* LocalPlayer = PlayerController->GetLocalPlayer();
	return LocalPlayer
		? LocalPlayer->GetSubsystem<ULocalPlayerUILayerSubsystem>()
		: nullptr;
}

void UPartnerHackComponent::AddHackCandidate(AActor* Actor, UHackableComponent* HackableComponent, const FVector2D& ScreenLocation)
{
	if (!Actor || !HackableComponent)
	{
		return;
	}

	HackCandidateActors.Add(Actor);
	HackCandidateComponents.Add(HackableComponent);

	if (CandidateLayerWidget)
	{
		CandidateLayerWidget->AddCandidate(Actor, HackableComponent);
	}

	if (bDebugHack)
	{
		UE_LOG(LogTemp, Warning, TEXT("[PartnerHackDebug] Candidate added Actor=%s Hackable=%s Screen=(%.1f, %.1f) Tags=%s"),
			*GetNameSafe(Actor),
			*GetNameSafe(HackableComponent),
			ScreenLocation.X,
			ScreenLocation.Y,
			*HackableComponent->HackTags.ToStringSimple());
	}
}

void UPartnerHackComponent::RemoveHackCandidateAt(int32 Index)
{
	if (!HackCandidateComponents.IsValidIndex(Index))
	{
		return;
	}

	UHackableComponent* HackableComponent = HackCandidateComponents[Index];
	AActor* Actor = HackCandidateActors.IsValidIndex(Index) ? HackCandidateActors[Index] : nullptr;

	if (CandidateLayerWidget)
	{
		CandidateLayerWidget->RemoveCandidate(Actor, HackableComponent);
	}

	if (Actor && MarkerHoveredActor == Actor)
	{
		MarkerHoveredWidget = nullptr;
		MarkerHoveredActor = nullptr;
	}
	if (Actor && HoveredHackActor == Actor)
	{
		ResetLocalHackHoldProgress();
		HoveredMarkerWidget = nullptr;
		HoveredHackActor = nullptr;
		RefreshCursorHighlight();
	}

	if (bDebugHack)
	{
		UE_LOG(LogTemp, Warning, TEXT("[PartnerHackDebug] Candidate removed Actor=%s Hackable=%s"),
			*GetNameSafe(Actor),
			*GetNameSafe(HackableComponent));
	}

	HackCandidateComponents.RemoveAtSwap(Index);
	if (HackCandidateActors.IsValidIndex(Index))
	{
		HackCandidateActors.RemoveAtSwap(Index);
	}
}

void UPartnerHackComponent::DeactivateUnselectedCandidates(UHackableComponent* SelectedComponent)
{
	for (int32 Index = HackCandidateComponents.Num() - 1; Index >= 0; --Index)
	{
		if (HackCandidateComponents[Index] != SelectedComponent)
		{
			RemoveHackCandidateAt(Index);
		}
	}
}

void UPartnerHackComponent::CancelActiveHack()
{
	ResetLocalHackHoldProgress();
	const uint32 RequestId = ActiveHackRequestId;
	UHackableComponent* CancelledHackableComponent = ActiveHackableComponent;

	if (HackMiniGameWidget)
	{
		HackMiniGameWidget->CancelHacking();
	}

	// 위젯 취소가 완료 알림을 동기 호출한다. 그 안에서 시작한 새 요청은 취소하지 않는다.
	if (!ActiveHackableComponent || ActiveHackableComponent != CancelledHackableComponent
		|| ActiveHackRequestId != RequestId)
	{
		return;
	}

	FHackResultContext ResultContext;
	ResultContext.RequestId = ActiveHackRequestId;
	ResultContext.TargetActor = ActiveHackableComponent->GetOwner();
	ResultContext.InstigatorActor = PartnerCharacter;
	ResultContext.Result = EHackResult::Cancelled;
	ServerCompleteHack(ResultContext);
}

void UPartnerHackComponent::SetActiveHackableComponent(UHackableComponent* HackableComponent)
{
	if (ActiveHackableComponent == HackableComponent)
	{
		return;
	}

	ClearActiveHackableComponent();
	ActiveHackableComponent = HackableComponent;
	ActivePoolHackEnemy = HackableComponent ? Cast<AEnemyBase>(HackableComponent->GetOwner()) : nullptr;
	if (AEnemyBase* Enemy = ActivePoolHackEnemy.Get(); Enemy && Enemy->IsPoolManaged())
	{
		ActiveHackGeneration = Enemy->GetPoolGameplayGeneration();
		ActiveHackLease = Enemy->GetPoolLeaseSerial();
	}
	// 클라이언트에서는 PoolState 복제 전에 시작 RPC가 올 수 있어 Enemy 참조 자체는 유지한다.

	if (IsValid(ActiveHackableComponent))
	{
		ActiveHackableComponent->OnHackTargetInvalidated.AddUObject(
			this,
			&UPartnerHackComponent::HandleHackTargetInvalidated);
		ActiveHackableComponent->OnHackTargetUnavailable.AddUObject(
			this,
			&UPartnerHackComponent::HandleHackTargetUnavailable);
	}
}

void UPartnerHackComponent::ClearActiveHackableComponent()
{
	if (ActiveHackableComponent)
	{
		ActiveHackableComponent->OnHackTargetInvalidated.RemoveAll(this);
		ActiveHackableComponent->OnHackTargetUnavailable.RemoveAll(this);
	}

	ActiveHackableComponent = nullptr;
	ActivePoolHackEnemy.Reset();
	ActiveHackGeneration = ActiveHackLease = 0;
}

bool UPartnerHackComponent::IsActiveHackLifetimeCurrent() const
{
	if (!ActiveHackLease) return true;
	const AEnemyBase* Enemy = ActivePoolHackEnemy.Get();
	if (Enemy && GetOwner() && !GetOwner()->HasAuthority() && Enemy->GetPoolLeaseSerial() < ActiveHackLease)
	{
		// 서버의 시작 RPC가 Pawn 복제보다 빠르면 아직 옛 번호가 보인다. 새 번호를 관측할 때까지 기다린다.
		return true;
	}
	// 반환 알림은 같은 대여의 취소를 전달해야 하므로, 여기서는 번호만 비교한다.
	return Enemy && Enemy->MatchesPoolLease(ActiveHackGeneration, ActiveHackLease);
}

bool UPartnerHackComponent::IsServerHoldLifetimeCurrent() const
{
	if (!ServerHoldLease) return true;
	const AEnemyBase* Enemy = ServerHeldPoolEnemy.Get();
	return Enemy && !Enemy->IsPoolReturnInProgress()
		&& Enemy->MatchesPoolLease(ServerHoldGeneration, ServerHoldLease);
}

void UPartnerHackComponent::AbortLocalHackForInvalidTarget()
{
	ClearActiveHackableComponent();
	ResetLocalHackHoldProgress();
	HoveredMarkerWidget = nullptr;
	HoveredHackActor = nullptr;
	bHackCandidateSearchActive = false;

	
	DestroyHackMiniGameWidget();
	DestroyCandidateLayerWidget();
	ClearHackCandidates();
	LastDebugCandidateCount = INDEX_NONE;
}

void UPartnerHackComponent::HandleHackTargetUnavailable(UHackableComponent* UnavailableComponent)
{
	if (UnavailableComponent != ActiveHackableComponent)
	{
		return;
	}
	if (GetOwner() && !GetOwner()->HasAuthority() && ActiveHackLease)
	{
		const AEnemyBase* Enemy = ActivePoolHackEnemy.Get();
		if (Enemy && Enemy->GetPoolLeaseSerial() < ActiveHackLease)
		{
			// 옛 대여의 Locked 복제가 새 미니게임 RPC보다 늦을 수 있다. 현재 요청의 종료는 서버가 보낸다.
			return;
		}
	}
	if (GetOwner() && GetOwner()->HasAuthority())
	{
		FHackResultContext CancelledContext;
		CancelledContext.RequestId = ActiveHackRequestId;
		CancelledContext.TargetActor = UnavailableComponent->GetOwner();
		CancelledContext.Result = EHackResult::Cancelled;
		CompleteActiveHack(CancelledContext, true);
	}
	else
	{
		AbortLocalHackForInvalidTarget();
	}
}

void UPartnerHackComponent::HandleHackTargetInvalidated(
	UHackableComponent* InvalidatedComponent,
	EEndPlayReason::Type EndPlayReason)
{
	if (InvalidatedComponent != ActiveHackableComponent)
	{
		return;
	}

	if (bDebugHack)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[PartnerHackDebug] Hack failed: active target ended Target=%s EndPlayReason=%d"),
			*GetNameSafe(InvalidatedComponent ? InvalidatedComponent->GetOwner() : nullptr),
			static_cast<int32>(EndPlayReason));
	}

	const bool bHasAuthority = GetOwner() && GetOwner()->HasAuthority();
	if (bHasAuthority)
	{
		FHackResultContext CancelledContext;
		CancelledContext.RequestId = ActiveHackRequestId;
		CancelledContext.TargetActor = InvalidatedComponent->GetOwner();
		CancelledContext.InstigatorActor = PartnerCharacter;
		CancelledContext.Result = EHackResult::Cancelled;
		CompleteActiveHack(CancelledContext, true);
	}
	else
	{
		AbortLocalHackForInvalidTarget();
	}
}

void UPartnerHackComponent::StartHackMiniGame(AActor* TargetActor, UHackableComponent* HackableComponent,
	float ClickSuccessRangeMultiplier, float SpinningGapMultiplier)
{
	if (!PartnerCharacter || !PartnerCharacter->IsLocallyControlled())
	{
		return;
	}

	if (EnsureHackMiniGameWidget(TargetActor, HackableComponent, ClickSuccessRangeMultiplier, SpinningGapMultiplier))
	{
		return;
	}

	FHackResultContext ResultContext;
	ResultContext.RequestId = ActiveHackRequestId;
	ResultContext.TargetActor = TargetActor;
	ResultContext.InstigatorActor = PartnerCharacter;
	ResultContext.Result = EHackResult::Fail;
	ServerCompleteHack(ResultContext);
	DestroyHackMiniGameWidget();
}

void UPartnerHackComponent::HandleHackMiniGameFinished(const FHackResultContext& ResultContext)
{
	// 이전 위젯의 지연 알림은 현재 요청의 UI까지 닫지 않는다.
	if (ResultContext.RequestId != ActiveHackRequestId) return;
	DestroyHackMiniGameWidget();

	ServerCompleteHack(ResultContext);
}
