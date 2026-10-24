#include "Drone/Partner/PartnerEMPComponent.h"
#include "Audio/OutlierAudioFeedback.h"
#include "Drone/Partner/EMPGameplayTags.h"
#include "Drone/Partner/EMPableComponent.h"
#include "Drone/Partner/PartnerCharacter.h"
#include "AbilitySystemInterface.h"
#include "Engine/LocalPlayer.h"
#include "Engine/OverlapResult.h"
#include "GAS/OutlierAbilitySystemComponent.h"
#include "Drone/Partner/PartnerPlayerController.h"
#include "GameFramework/PlayerController.h"
#include "Interface/EMPableInterface.h"
#include "UI/EMPLayerWidget.h"
#include "UI/EMPMarkWidget.h"
#include "UI/LocalPlayerUILayerSubsystem.h"
#include "UI/UILayerGameplayTags.h"
#include "FirstPerson/FirstPersonPlayerController.h"
#include "Net/UnrealNetwork.h"
#include "GameplayTags/OutlierGameplayTags.h"

UPartnerEMPComponent::UPartnerEMPComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	SetIsReplicatedByDefault(true);
}

void UPartnerEMPComponent::BeginPlay()
{
	Super::BeginPlay();

	CachedAbilityData.EMPRange = EMPRange;
	ResetEMPEarlyCompleteTimer();

}

void UPartnerEMPComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ResetEMPEarlyCompleteTimer();
	ResetAutoTarget();
	DestroyEMPLayerWidget();
	ClearEMPCandidates();
	Super::EndPlay(EndPlayReason);
}

void UPartnerEMPComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(UPartnerEMPComponent, bEMPActive);
}

void UPartnerEMPComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (bAutoTargetActive)
	{
		if (PartnerCharacter && PartnerCharacter->HasAuthority() && bEMPActive && !bAwaitingAutoTargetSnapshot)
			AdvanceAutoTargetPending(GetWorld()->GetTimeSeconds() - AutoTargetStartTime);
		return;
	}
	if (!bEMPCandidateSearchActive)
	{
		return;
	}

	if (!PartnerCharacter || !PartnerCharacter->IsLocallyControlled())
	{
		return;
	}

	RefreshEMPCandidates();
}

void UPartnerEMPComponent::TryEMP_Implementation()
{
	if (!PartnerCharacter || !GetWorld() || !PartnerCharacter->CanAcceptInput())
	{
		return;
	}

	if (bAutoTargetActive) return; // 자동 선택 중 재입력으로 조기 실행하지 않는다.

	const UOutlierAbilitySystemComponent* AbilitySystem =
		PartnerCharacter->GetOutlierAbilitySystemComponent();
	if (!AbilitySystem || !AbilitySystem->IsPartnerAbilitiesConfigured())
	{
		return;
	}
	const FOutlierPartnerAbilityConfig& Config = AbilitySystem->GetPartnerAbilityConfig();
	EMPMarkingTime = Config.MarkDuration;
	ActiveStunDuration = Config.StunDuration;

	if (bEMPActive)
	{
		const float ElapsedTime = GetEMPElapsedTime();
		const float EarlyCompleteDelay = FMath::Max(EMPEarlyCompleteValue, 0.0f);

		if (MarkedActors.Num() > 0 || ElapsedTime >= EarlyCompleteDelay)
		{
			const TArray<AActor*> ConfirmedActors = MarkedActors;
			CompleteEMPOnServer(ConfirmedActors);
		}
		return;
	}

	bEMPActive = true;
	MarkedActors.Empty();
	InitializeEMPEarlyCompleteTimer();
	// 리슨 호스트에서는 대상 수집 요청 안에서 EMP가 즉시 종료될 수 있다.
	// 먼저 HUD를 숨겨 종료 경로의 복원이 마지막에 적용되도록 한다.
	DefaultWidgetControl(true);
	const FGameplayTag AutoTargetTag = FGameplayTag::RequestGameplayTag(TEXT("Upgrade.Partner.EMP.AutoTarget"));
	if (AbilitySystem->HasMatchingGameplayTag(AutoTargetTag))
	{
		bAutoTargetActive = true;
		bAwaitingAutoTargetSnapshot = true;
		AutoTargetPendingTime = Config.EMPAutoTargetPendingTime;
		AutoTargetOrigin = PartnerCharacter->GetActorLocation();
		AutoTargetForward = PartnerCharacter->GetActorForwardVector();
		++AutoTargetSessionId;
		ClientStartAutoTargetEMP(AutoTargetPendingTime, AutoTargetSessionId);
	}
	else
	{
		ClientStartEMPSearch(EMPMarkingTime);
	}
}

void UPartnerEMPComponent::SortByForwardDepth(TArray<AActor*>& Actors, const FVector& Origin, const FVector& Forward)
{
	Actors.StableSort([&](const AActor& A, const AActor& B)
	{
		return FVector::DotProduct(A.GetActorLocation() - Origin, Forward)
			< FVector::DotProduct(B.GetActorLocation() - Origin, Forward);
	});
}

void UPartnerEMPComponent::GatherAutoTargetSnapshot(TArray<AActor*>& OutActors) const
{
	OutActors.Reset();
	if (!PartnerCharacter || !GetWorld()) return;
	FCollisionObjectQueryParams Objects;
	Objects.AddObjectTypesToQuery(ECC_Pawn);
	Objects.AddObjectTypesToQuery(ECC_WorldDynamic);
	Objects.AddObjectTypesToQuery(ECC_PhysicsBody);
	FCollisionQueryParams Query(SCENE_QUERY_STAT(PartnerEMPAutoSnapshot), false);
	Query.AddIgnoredActor(PartnerCharacter);
	TArray<FOverlapResult> Overlaps;
	GetWorld()->OverlapMultiByObjectType(Overlaps, PartnerCharacter->GetActorLocation(), FQuat::Identity,
		Objects, FCollisionShape::MakeSphere(EMPRange), Query);
	for (const FOverlapResult& Overlap : Overlaps)
	{
		AActor* Actor = Overlap.GetActor();
		FVector2D ScreenLocation;
		if (IsValid(Actor) && IsCandidateActorValid(Actor, ResolveEMPableComponent(Actor), ScreenLocation))
			OutActors.AddUnique(Actor);
	}
	SortByForwardDepth(OutActors, PartnerCharacter->GetActorLocation(), PartnerCharacter->GetActorForwardVector());
}

void UPartnerEMPComponent::ClientStartAutoTargetEMP_Implementation(float PendingTime, uint32 SessionId)
{
	if (!PartnerCharacter || !PartnerCharacter->IsLocallyControlled()) return;
	bAutoTargetActive = true;
	AutoTargetSessionId = SessionId;
	bEMPCandidateSearchActive = true;
	ClearEMPCandidates();
	TArray<AActor*> Snapshot;
	GatherAutoTargetSnapshot(Snapshot); // 최초 한 번만 캡처. 이후 HUD 검색 갱신은 차단한다.
	const float DisplayDuration = Snapshot.IsEmpty() ? EMPInitialCaptureEmptyTimeout : PendingTime;
	EnsureEMPLayerWidget();
	if (EMPLayerWidget) EMPLayerWidget->InitializeMarkingTimer(DisplayDuration);
	ServerSetAutoTargetSnapshot(Snapshot, SessionId);
}

void UPartnerEMPComponent::ServerSetAutoTargetSnapshot_Implementation(const TArray<AActor*>& Actors, uint32 SessionId)
{
	if (!bEMPActive || !bAutoTargetActive || !bAwaitingAutoTargetSnapshot || SessionId != AutoTargetSessionId) return;
	TArray<AActor*> ValidatedActors;
	for (AActor* Actor : Actors)
	{
		FVector2D ScreenLocation;
		if (IsValid(Actor) && IsCandidateActorValid(Actor, ResolveEMPableComponent(Actor), ScreenLocation))
			ValidatedActors.AddUnique(Actor);
	}
	BeginAutoTargetPending(ValidatedActors);
}

void UPartnerEMPComponent::BeginAutoTargetPending(const TArray<AActor*>& Actors)
{
	TArray<AActor*> Sorted = Actors;
	SortByForwardDepth(Sorted, AutoTargetOrigin, AutoTargetForward);
	AutoTargetSnapshot.Reset();
	for (AActor* Actor : Sorted) AutoTargetSnapshot.Add(Actor);
	// 서버 검증 후에도 대상이 없으면 기본 EMP와 같은 짧은 대기 시간으로 종료한다.
	if (AutoTargetSnapshot.IsEmpty()) AutoTargetPendingTime = FMath::Max(EMPInitialCaptureEmptyTimeout, 0.0f);
	NextAutoTargetIndex = 0;
	bAwaitingAutoTargetSnapshot = false;
	AutoTargetStartTime = GetWorld()->GetTimeSeconds();
	AdvanceAutoTargetPending(0.0f);
}

void UPartnerEMPComponent::AdvanceAutoTargetPending(float ElapsedTime)
{
	if (!bEMPActive || !bAutoTargetActive || bAwaitingAutoTargetSnapshot) return;
	const int32 Count = AutoTargetSnapshot.Num();
	const float Interval = Count > 0 ? AutoTargetPendingTime / Count : 0.0f;
	// 첫 대상은 즉시 선택하고 마지막 선택 뒤에도 한 슬롯을 남긴다.
	// 선택음과 EMP 폭발음이 같은 시점에 시작되지 않도록 한다.
	// 프레임이 늦어져도 경과 시간에 해당하는 슬롯을 순서대로 모두 처리한다.
	while (NextAutoTargetIndex < Count
		&& ElapsedTime + KINDA_SMALL_NUMBER >= Interval * NextAutoTargetIndex)
	{
		AActor* Actor = AutoTargetSnapshot[NextAutoTargetIndex++].Get();
		if (!IsValid(Actor)) continue; // 슬롯 간격은 고정. 소멸 대상은 건너뛴다.
		MarkedActors.AddUnique(Actor);
		ClientRevealAutoTarget(Actor, AutoTargetSessionId);
	}
	if (ElapsedTime + KINDA_SMALL_NUMBER >= AutoTargetPendingTime)
	{
		const TArray<AActor*> Selected = MarkedActors;
		CompleteEMPOnServer(Selected);
	}
}

void UPartnerEMPComponent::ClientRevealAutoTarget_Implementation(AActor* Actor, uint32 SessionId)
{
	if (!bAutoTargetActive || SessionId != AutoTargetSessionId || !IsValid(Actor)) return;
	UEMPableComponent* EMPable = ResolveEMPableComponent(Actor);
	if (!EMPable) return;
	// 마크 위젯 생성 여부와 무관하게 소유 클라이언트에서 선택음을 재생한다.
	OutlierAudioFeedback::Local(this, TEXT("Audio.Context.Player.02.Skill.Emp.Select"), TEXT("Audio.Type.Player"));
	AddEMPCandidate(Actor, EMPable, FVector2D::ZeroVector);
	if (EMPLayerWidget) EMPLayerWidget->SelectAutomaticCandidate(Actor);
}

void UPartnerEMPComponent::ResetAutoTarget()
{
	bAutoTargetActive = false;
	bAwaitingAutoTargetSnapshot = false;
	AutoTargetSnapshot.Reset();
	NextAutoTargetIndex = 0;
	AutoTargetStartTime = 0.0f;
}

void UPartnerEMPComponent::CacheAbilityData(const FPartnerEMPAbilityData& InAbilityData)
{
	CachedAbilityData = InAbilityData;

	EMPRange = CachedAbilityData.EMPRange;
}

void UPartnerEMPComponent::ClientStartEMPSearch_Implementation(float InMarkingTime)
{
	if (!PartnerCharacter || !PartnerCharacter->IsLocallyControlled())
	{
		return;
	}

	EMPMarkingTime = FMath::Max(InMarkingTime, 0.0f);

	bEMPCandidateSearchActive = true;

	EnsureEMPLayerWidget();
	RefreshEMPCandidates(); //Capture

	if (EMPLayerWidget)
	{
		const bool bInitialCaptureEmpty = EMPCandidateActors.IsEmpty(); //Capture 시 없으면, N초 뒤에 Widget 삭제 및, EMP 종료.
		const float ExpirationTime = bInitialCaptureEmpty
			? EMPInitialCaptureEmptyTimeout // 조기 종료 값
			: EMPMarkingTime;

		EMPLayerWidget->InitializeMarkingTimer(ExpirationTime);
	}

}

void UPartnerEMPComponent::ClientStopEMPSearch_Implementation()
{
	if (!PartnerCharacter || !PartnerCharacter->IsLocallyControlled())
	{
		return;
	}

	StopEMPCandidateSearch();
}

void UPartnerEMPComponent::DefaultWidgetControl_Implementation(bool InFlag)
{
	if (AFirstPersonPlayerController* Controller = Cast<AFirstPersonPlayerController>(PartnerCharacter->GetController()))
	{
		Controller->ControlMainWidget(!InFlag);
	}
}

void UPartnerEMPComponent::RefreshEMPCandidates()
{
	if (bAutoTargetActive) return;
	if (!bEMPCandidateSearchActive)
	{
		return;
	}

	if (!PartnerCharacter || !GetWorld())
	{
		ClearEMPCandidates();
		return;
	}

	DeduplicateEMPCandidates();

	TArray<FOverlapResult> OverlapResults;

	FCollisionObjectQueryParams ObjectParams;
	ObjectParams.AddObjectTypesToQuery(ECC_Pawn);
	ObjectParams.AddObjectTypesToQuery(ECC_WorldDynamic);
	ObjectParams.AddObjectTypesToQuery(ECC_PhysicsBody);

	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(PartnerEMPCandidateOverlap), false);
	QueryParams.AddIgnoredActor(PartnerCharacter);

	GetWorld()->OverlapMultiByObjectType(
		OverlapResults,
		PartnerCharacter->GetActorLocation(),
		FQuat::Identity,
		ObjectParams,
		FCollisionShape::MakeSphere(EMPRange),
		QueryParams
	);

	TArray<TObjectPtr<AActor>> NextCandidateActors;
	TArray<TObjectPtr<UEMPableComponent>> NextCandidateComponents;

	for (const FOverlapResult& Result : OverlapResults)
	{
		AActor* Actor = Result.GetActor();
		UEMPableComponent* EMPableComp = ResolveEMPableComponent(Actor);
		FVector2D ScreenLocation = FVector2D::ZeroVector;

		if (!IsCandidateActorValid(Actor, EMPableComp, ScreenLocation))
		{
			continue;
		}

		if (NextCandidateActors.Contains(Actor) || NextCandidateComponents.Contains(EMPableComp))
		{
			continue;
		}

		NextCandidateActors.Add(Actor);
		NextCandidateComponents.Add(EMPableComp);

		if (!EMPCandidateActors.Contains(Actor) && !EMPCandidateComponents.Contains(EMPableComp))
		{
			AddEMPCandidate(Actor, EMPableComp, ScreenLocation);
		}
	}

	for (int32 Index = EMPCandidateComponents.Num() - 1; Index >= 0; --Index)
	{
		const bool bActorStillCandidate = EMPCandidateActors.IsValidIndex(Index) && NextCandidateActors.Contains(EMPCandidateActors[Index]);
		const bool bComponentStillCandidate = NextCandidateComponents.Contains(EMPCandidateComponents[Index]);
		if (!bActorStillCandidate || !bComponentStillCandidate)
		{
			RemoveEMPCandidateAt(Index);
		}
	}

	DeduplicateEMPCandidates();

}

void UPartnerEMPComponent::ClearEMPCandidates()
{
	for (int32 Index = EMPCandidateComponents.Num() - 1; Index >= 0; --Index)
	{
		RemoveEMPCandidateAt(Index);
	}
}

void UPartnerEMPComponent::StopEMPCandidateSearch()
{
	if (!PartnerCharacter || !PartnerCharacter->HasAuthority() || !bEMPActive) ResetAutoTarget();
	bEMPCandidateSearchActive = false;
	ClearEMPCandidates();
	DestroyEMPLayerWidget();
}

void UPartnerEMPComponent::RefocusEMPInput()
{
	if (ULocalPlayerUILayerSubsystem* LayerSubsystem = GetUILayerSubsystem())
	{
		LayerSubsystem->RefocusLayer(
			EMPLayerHandle,
			EUILayerFocusTarget::GameViewport);
	}
}

void UPartnerEMPComponent::TryMarkEMPTarget_Implementation(AActor* TargetActor)
{
	if (bAutoTargetActive) return;
	if (!TargetActor || !PartnerCharacter || !PartnerCharacter->CanAcceptInput())
	{
		return;
	}

	if (!bEMPActive)
	{
		return;
	}

	UEMPableComponent* EMPableComponent = ResolveEMPableComponent(TargetActor);
	FVector2D ScreenLocation = FVector2D::ZeroVector;
	if (!IsCandidateActorValid(TargetActor, EMPableComponent, ScreenLocation))
	{

		return;
	}

	if (!MarkedActors.Contains(TargetActor)
		&& CachedAbilityData.MaxTargets > 0
		&& MarkedActors.Num() >= CachedAbilityData.MaxTargets)
	{

		return;
	}

	MarkedActors.AddUnique(TargetActor);
}

bool UPartnerEMPComponent::NotifyEMPConfirmed()
{
	if (bAutoTargetActive) return true; // 소비하되 실행 요청은 보내지 않는다.
	if (!bEMPCandidateSearchActive)
	{
		return false;
	}

	TryEMP();
	return true;
}

void UPartnerEMPComponent::NotifyEMPExpired()
{
	if (bAutoTargetActive) return; // 자동 모드는 서버 Pending 타이머로 종료한다.
	if (!bEMPCandidateSearchActive)
	{
		return;
	}

	StopEMPCandidateSearch();
	ServerExpireEMP();
}

void UPartnerEMPComponent::ServerCompleteEMP_Implementation(const TArray<AActor*>& InMarkedActors)
{
	if (!bAutoTargetActive) CompleteEMPOnServer(InMarkedActors);
}

void UPartnerEMPComponent::CompleteEMPOnServer(const TArray<AActor*>& InMarkedActors)
{
	if (!bEMPActive)
	{
		return;
	}

	if (!PartnerCharacter || !PartnerCharacter->CanAcceptInput())
	{
		CancelEMPOnServer();
		return;
	}

	if (InMarkedActors.Num() <= 0)
	{

		bEMPActive = false;
		ResetAutoTarget();
		MarkedActors.Empty();
		ResetEMPEarlyCompleteTimer();
		ClientCompleteEMP();
		DefaultWidgetControl(false);
		OnEMPFinished.Broadcast(false, false);
		return;
	}

	int32 AppliedTargetCount = 0;
	for (AActor* MarkedActor : InMarkedActors)
	{
		if (!bAutoTargetActive && CachedAbilityData.MaxTargets > 0 && AppliedTargetCount >= CachedAbilityData.MaxTargets)
		{
			break;
		}

		if (!IsValid(MarkedActor))
		{
			continue;
		}

		UEMPableComponent* EMPableComponent = ResolveEMPableComponent(MarkedActor);
		FVector2D ScreenLocation = FVector2D::ZeroVector;
		// 자동 모드는 최초 명단을 유지한다. 이동/시점 변경으로 대상을 다시 고르지 않는다.
		const bool bEligible = bAutoTargetActive
			? (EMPableComponent && EMPableComponent->IsEMPTargetType()
				&& EMPableComponent->CanBeEMPTarget(RequiredEMPTags, BlockedEMPTags))
			: IsCandidateActorValid(MarkedActor, EMPableComponent, ScreenLocation);
		if (!bEligible)
		{
			continue;
		}

		IAbilitySystemInterface* AbilitySystemActor = Cast<IAbilitySystemInterface>(MarkedActor);
		UOutlierAbilitySystemComponent* TargetAbilitySystem = AbilitySystemActor
			? Cast<UOutlierAbilitySystemComponent>(AbilitySystemActor->GetAbilitySystemComponent())
			: nullptr;
		if (!TargetAbilitySystem
			|| !TargetAbilitySystem->ApplyStunStateToSelf(
				ActiveStunDuration,
				PartnerCharacter).IsValid())
		{
			UE_LOG(
				LogTemp,
				Warning,
				TEXT("[PartnerEMP] Target has no compatible ASC or rejected Stun GameplayEffect. Target=%s Duration=%.2f"),
				*GetNameSafe(MarkedActor),
				ActiveStunDuration);
			continue;
		}

		++AppliedTargetCount;
	}

	bEMPActive = false;
	ResetAutoTarget();
	MarkedActors.Empty();
	ResetEMPEarlyCompleteTimer();
	ClientCompleteEMP();
	DefaultWidgetControl(false);
	OnEMPFinished.Broadcast(AppliedTargetCount > 0, false);
}

void UPartnerEMPComponent::ServerCancelEMP_Implementation()
{
	if (!bAutoTargetActive) CancelEMPOnServer();
}

void UPartnerEMPComponent::ServerExpireEMP_Implementation()
{
	if (bAutoTargetActive) return;

	if (!MarkedActors.IsEmpty())
	{
		const TArray<AActor*> ExpiredMarkedActors = MarkedActors;
		CompleteEMPOnServer(ExpiredMarkedActors);
		return;
	}

	CancelEMPOnServer();
}

void UPartnerEMPComponent::CancelEMPOnServer()
{
	const bool bWasActive = bEMPActive;

	bEMPActive = false;
	ResetAutoTarget();
	MarkedActors.Empty();
	ResetEMPEarlyCompleteTimer();
	ClientCompleteEMP();
	DefaultWidgetControl(false);
	if (bWasActive)
	{
		OnEMPFinished.Broadcast(false, true);
	}
}

void UPartnerEMPComponent::ClientCompleteEMP_Implementation()
{
	StopEMPCandidateSearch();
}

void UPartnerEMPComponent::CancelForReboot()
{
	if (!PartnerCharacter || !PartnerCharacter->HasAuthority())
	{
		return;
	}

	CancelEMPOnServer();
	StopEMPCandidateSearch();
}

UEMPableComponent* UPartnerEMPComponent::ResolveEMPableComponent(AActor* Actor) const
{
	if (!Actor || Actor == PartnerCharacter)
	{
		return nullptr;
	}

	if (!Actor->GetClass()->ImplementsInterface(UEMPableInterface::StaticClass()))
	{
		return nullptr;
	}

	UEMPableComponent* EMPableComp = Cast<IEMPableInterface>(Actor)->GetEMPableComponent();

	return EMPableComp;
}

bool UPartnerEMPComponent::IsCandidateActorValid(AActor* Actor, UEMPableComponent* EMPableComp, FVector2D& OutScreenLocation) const
{
	if (!Actor || !PartnerCharacter || !EMPableComp || !EMPableComp->IsEMPTargetType())
	{
		return false;
	}

	if (FVector::DistSquared(PartnerCharacter->GetActorLocation(), Actor->GetActorLocation()) > FMath::Square(EMPRange))
	{
		return false;
	}

	if (Actor->GetWorld() != GetWorld())
	{

		return false;
	}

	if (!EMPableComp->CanBeEMPTarget(RequiredEMPTags, BlockedEMPTags))
	{
		return false;
	}

	if (bRequireLineOfSight && !HasLineOfSight(Actor))
	{
		return false;
	}

	if (!IsActorInViewport(Actor, OutScreenLocation))
	{
		return false;
	}

	return true;
}

bool UPartnerEMPComponent::IsActorInViewport(AActor* Actor, FVector2D& OutScreenLocation) const
{
	if (!Actor || !PartnerCharacter)
	{
		return false;
	}

	APlayerController* PlayerController = Cast<APlayerController>(PartnerCharacter->GetController());
	if (!PlayerController || !PlayerController->IsLocalController())
	{
		OutScreenLocation = FVector2D::ZeroVector;
		return true;
	}

	if (!PlayerController->ProjectWorldLocationToScreen(Actor->GetActorLocation(), OutScreenLocation, true))
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

bool UPartnerEMPComponent::HasLineOfSight(AActor* Actor) const
{
	if (!Actor || !PartnerCharacter || !GetWorld())
	{
		return false;
	}

	FHitResult Hit;
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(PartnerEMPLineOfSight), false);
	QueryParams.AddIgnoredActor(PartnerCharacter);

	FVector ViewLocation;
	FRotator ViewRotation;
	if (AController* Controller = PartnerCharacter->GetController())
	{
		Controller->GetPlayerViewPoint(ViewLocation, ViewRotation);
	}
	else
	{
		ViewLocation = PartnerCharacter->GetActorLocation();
	}

	const bool bHit = GetWorld()->LineTraceSingleByChannel(
		Hit,
		ViewLocation,
		Actor->GetActorLocation(),
		ECC_Visibility,
		QueryParams
	);

	return !bHit || Hit.GetActor() == Actor;
}

void UPartnerEMPComponent::InitializeEMPEarlyCompleteTimer()
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		ResetEMPEarlyCompleteTimer();
		return;
	}

	EMPStartTimeSeconds = World->GetTimeSeconds();
}

void UPartnerEMPComponent::ResetEMPEarlyCompleteTimer()
{
	EMPStartTimeSeconds = 0.0f;
}

float UPartnerEMPComponent::GetEMPElapsedTime() const
{
	const UWorld* World = GetWorld();
	if (!World || !bEMPActive)
	{
		return 0.0f;
	}

	return FMath::Max(World->GetTimeSeconds() - EMPStartTimeSeconds, 0.0f);
}

void UPartnerEMPComponent::EnsureEMPLayerWidget()
{
	if (IsValid(EMPLayerWidget))
	{
		return;
	}

	if (!PartnerCharacter)
	{
		return;
	}

	APlayerController* PlayerController = Cast<APlayerController>(PartnerCharacter->GetController());
	if (!PlayerController || !PlayerController->IsLocalController())
	{
		return;
	}

	TSubclassOf<UEMPLayerWidget> EffectiveClass = EMPLayerWidgetClass;
	if (!EffectiveClass)
	{
		EffectiveClass = UEMPLayerWidget::StaticClass();
	}

	EMPLayerWidget = CreateWidget<UEMPLayerWidget>(PlayerController, EffectiveClass);


	if (!IsValid(EMPLayerWidget))
	{
		return;
	}

	EMPLayerWidget->SetMarkWidgetClass(EMPMarkWidgetClass);
	EMPLayerWidget->BindEMPComponent(this);
	EMPLayerWidget->InitializeMarkingTimer(EMPMarkingTime);

	if (ULocalPlayerUILayerSubsystem* LayerSubsystem = GetUILayerSubsystem())
	{
		EMPLayerHandle = LayerSubsystem->PushWidget(
			UILayerTags::Gameplay(),
			EMPLayerWidget,
			FirstPersonInputModeTags::EMP(),
			this,
			bAutoTargetActive ? EUILayerFocusTarget::GameViewport : EUILayerFocusTarget::Widget,
			true);
	}

	if (!EMPLayerHandle.IsValid())
	{
		UE_LOG(LogTemp, Error,
			TEXT("[PartnerEMP] Failed to push EMP widget into the local UI layer"));
		EMPLayerWidget->BindEMPComponent(nullptr);
		EMPLayerWidget = nullptr;
	}
}

void UPartnerEMPComponent::DestroyEMPLayerWidget()
{

	if (!IsValid(EMPLayerWidget))
	{
		if (ULocalPlayerUILayerSubsystem* LayerSubsystem = GetUILayerSubsystem())
		{
			LayerSubsystem->PopLayer(EMPLayerHandle);
		}
		EMPLayerHandle.Reset();
		return;
	}

	EMPLayerWidget->ClearMarkers();
	EMPLayerWidget->SetVisibility(ESlateVisibility::Collapsed);
	EMPLayerWidget->BindEMPComponent(nullptr);

	bool bPoppedLayer = false;
	if (ULocalPlayerUILayerSubsystem* LayerSubsystem = GetUILayerSubsystem())
	{
		bPoppedLayer = LayerSubsystem->PopLayer(EMPLayerHandle);
	}
	if (!bPoppedLayer)
	{
		EMPLayerWidget->RemoveFromParent();
	}
	EMPLayerHandle.Reset();

	EMPLayerWidget = nullptr;

}

ULocalPlayerUILayerSubsystem* UPartnerEMPComponent::GetUILayerSubsystem() const
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

void UPartnerEMPComponent::AddEMPCandidate(AActor* Actor, UEMPableComponent* EMPableComp, const FVector2D& ScreenLocation)
{
	if (!Actor || !EMPableComp)
	{
		return;
	}

	if (EMPCandidateActors.Contains(Actor) || EMPCandidateComponents.Contains(EMPableComp))
	{
		return;
	}

	EMPCandidateActors.Add(Actor);
	EMPCandidateComponents.Add(EMPableComp);

	if (EMPLayerWidget)
	{
		EMPLayerWidget->AddCandidate(Actor, EMPableComp);
	}
}

void UPartnerEMPComponent::RemoveEMPCandidateAt(int32 Index)
{
	if (!EMPCandidateComponents.IsValidIndex(Index))
	{
		return;
	}

	UEMPableComponent* EMPableComp = EMPCandidateComponents[Index];
	AActor* Actor = EMPCandidateActors.IsValidIndex(Index) ? EMPCandidateActors[Index] : nullptr;

	if (EMPLayerWidget)
	{
		EMPLayerWidget->RemoveCandidate(Actor, EMPableComp);
	}

	EMPCandidateComponents.RemoveAtSwap(Index);
	if (EMPCandidateActors.IsValidIndex(Index))
	{
		EMPCandidateActors.RemoveAtSwap(Index);
	}
}

void UPartnerEMPComponent::DeduplicateEMPCandidates()
{
	TArray<TObjectPtr<AActor>> SeenActors;
	TArray<TObjectPtr<UEMPableComponent>> SeenComponents;

	for (int32 Index = EMPCandidateComponents.Num() - 1; Index >= 0; --Index)
	{
		AActor* Actor = EMPCandidateActors.IsValidIndex(Index) ? EMPCandidateActors[Index] : nullptr;
		UEMPableComponent* EMPableComp = EMPCandidateComponents[Index];

		if (!Actor || !EMPableComp || SeenActors.Contains(Actor) || SeenComponents.Contains(EMPableComp))
		{
			RemoveEMPCandidateAt(Index);
			continue;
		}

		SeenActors.Add(Actor);
		SeenComponents.Add(EMPableComp);
	}
}
