#include "FirstPerson/FirstPersonPlayerController.h"

#include "AbilitySystemInterface.h"
#include "Cinematic/OutlierCinematicSubsystem.h"
#include "Components/SkeletalMeshComponent.h"
#include "Drone/Partner/PartnerCharacter.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "FirstPerson/FirstPersonCharacter.h"
#include "GAS/OutlierAbilitySystemComponent.h"
#include "GameFramework/GameStateBase.h"
#include "MainUIBase.h"
#include "Network/OutlierArenaSubsystem.h"
#include "OutlierGameMode.h"
#include "SceneView.h"
#include "Shooter/ShooterCharacter.h"
#include "TimerManager.h"
#include "UI/LocalPlayerUILayerSubsystem.h"
#include "UI/PairPositionMarkerWidget.h"

void AFirstPersonPlayerController::BindPairMarkerSources(AOutlierPlayerState* State)
{
	if (PairMarkerPlayerState.Get() != State)
	{
		if (AOutlierPlayerState* Previous = PairMarkerPlayerState.Get())
		{
			Previous->OnPlayerCharactersChanged.Remove(PairMarkerCharactersHandle);
			Previous->OnPlayerRoleChanged.Remove(PairMarkerRoleHandle);
		}
		PairMarkerPlayerState = State;
		if (State)
		{
			PairMarkerCharactersHandle = State->OnPlayerCharactersChanged.AddUObject(this, &ThisClass::HandlePairMarkerLinksChanged);
			PairMarkerRoleHandle = State->OnPlayerRoleChanged.AddUObject(this, &ThisClass::HandlePairMarkerLinksChanged);
		}
	}
	if (!PairMarkerArena.IsValid())
	{
		if (UOutlierArenaSubsystem* Arena = GetWorld()->GetSubsystem<UOutlierArenaSubsystem>())
		{
			PairMarkerArena = Arena;
			PairMarkerReloadHandle = Arena->OnArenaGameplayReloadStarted.AddUObject(this, &ThisClass::HandlePairMarkerReload);
			PairMarkerReadyHandle = Arena->OnArenaGameplayReady.AddUObject(this, &ThisClass::HandlePairMarkerReady);
		}
	}
	if (!PairMarkerLayers.IsValid())
	{
		if (ULocalPlayer* LocalPlayer = GetLocalPlayer())
		{
			PairMarkerLayers = LocalPlayer->GetSubsystem<ULocalPlayerUILayerSubsystem>();
		}
	}
	HandlePairMarkerLinksChanged(State);
}

void AFirstPersonPlayerController::ClearPairMarkerSources()
{
	if (AOutlierPlayerState* State = PairMarkerPlayerState.Get())
	{
		State->OnPlayerCharactersChanged.Remove(PairMarkerCharactersHandle);
		State->OnPlayerRoleChanged.Remove(PairMarkerRoleHandle);
	}
	PairMarkerPlayerState.Reset();
	BindPairMarkerPeer(nullptr);
	BindPairMarkerDamageSource(nullptr);
	if (APawn* Target = PairMarkerTarget.Pawn.Get())
	{
		Target->OnDestroyed.RemoveDynamic(this, &ThisClass::HandlePairMarkerTargetDestroyed);
	}
	if (UOutlierArenaSubsystem* Arena = PairMarkerArena.Get())
	{
		Arena->OnArenaGameplayReloadStarted.Remove(PairMarkerReloadHandle);
		Arena->OnArenaGameplayReady.Remove(PairMarkerReadyHandle);
	}
	PairMarkerArena.Reset();
	PairMarkerLayers.Reset();
	GetWorldTimerManager().ClearTimer(PairMarkerSampleTimer);
	UpdatePairMarkerDamageAvailability(false);
	ReleasePairMarkerHud();
}

void AFirstPersonPlayerController::BindPairMarkerPeer(AOutlierPlayerState* Peer)
{
	if (PairMarkerPeer.Get() == Peer)
	{
		return;
	}
	if (AOutlierPlayerState* Previous = PairMarkerPeer.Get())
	{
		Previous->OnPlayerCharactersChanged.Remove(PairMarkerPeerCharactersHandle);
		Previous->OnPlayerRoleChanged.Remove(PairMarkerPeerRoleHandle);
	}
	PairMarkerPeer = Peer;
	PairMarkerPeerCharactersHandle.Reset();
	PairMarkerPeerRoleHandle.Reset();
	if (Peer)
	{
		PairMarkerPeerCharactersHandle = Peer->OnPlayerCharactersChanged.AddUObject(this, &ThisClass::HandlePairMarkerLinksChanged);
		PairMarkerPeerRoleHandle = Peer->OnPlayerRoleChanged.AddUObject(this, &ThisClass::HandlePairMarkerLinksChanged);
	}
}

void AFirstPersonPlayerController::HandlePairMarkerLinksChanged(AOutlierPlayerState* /*State*/)
{
	if (HasAuthority())
	{
		RefreshPairMarkerTarget();
	}
	RefreshPairMarkerPresentation();
}

void AFirstPersonPlayerController::HandlePairMarkerReload(uint32 /*GameplayGeneration*/)
{
	bPairMarkerReloading = true;
	UpdatePairMarkerDamageAvailability(false);
	if (HasAuthority())
	{
		SetPairMarkerTarget(nullptr, nullptr);
	}
	ReleasePairMarkerHud();
}

void AFirstPersonPlayerController::HandlePairMarkerReady(uint32 /*GameplayGeneration*/)
{
	bPairMarkerReloading = false;
	HandlePairMarkerLinksChanged(PairMarkerPlayerState.Get());
}

void AFirstPersonPlayerController::HandlePairMarkerTargetDestroyed(AActor* Actor)
{
	if (HasAuthority() && PairMarkerTarget.Pawn.Get() == Actor)
	{
		SetPairMarkerTarget(nullptr, nullptr);
	}
}

void AFirstPersonPlayerController::RefreshPairMarkerTarget()
{
	const AOutlierPlayerState* State = PairMarkerPlayerState.Get();
	const AOutlierGameMode* Mode = GetWorld()->GetAuthGameMode<AOutlierGameMode>();
	AOutlierPlayerState* Peer = State && Mode ? Mode->FindPairedPlayerState(*State) : nullptr;
	BindPairMarkerPeer(Peer);
	AFirstPersonCharacter* Target = nullptr;
	if (!bPairMarkerReloading && State && !State->IsOnlyASpectator()
		&& Peer && Peer->GetPlayerController() && !Peer->IsOnlyASpectator())
	{
		// 반대쪽 PlayerState도 같은 본체를 연결한 뒤에만 발행한다. 등록 순서가 엇갈리거나
		// 적 빙의로 Controller의 Pawn이 바뀌어도 상대 본체의 위치만 사용한다.
		if (State->IsShooterPlayer() && State->GetPartnerCharacter() == Peer->GetPartnerCharacter())
		{
			Target = Peer->GetPartnerCharacter();
		}
		else if (State->IsPartnerPlayer() && State->GetShooterCharacter() == Peer->GetShooterCharacter())
		{
			Target = Peer->GetShooterCharacter();
		}
	}
	if (!IsValid(Target) || Target->IsActorBeingDestroyed())
	{
		Target = nullptr;
	}
	SetPairMarkerTarget(Target, Target ? Peer : nullptr);
}

void AFirstPersonPlayerController::SetPairMarkerTarget(AFirstPersonCharacter* Target, const AOutlierPlayerState* Peer)
{
	const int32 PairId = Peer ? Peer->GetPairId() : INDEX_NONE;
	const EOutlierPlayerRole TargetRole = Peer ? Peer->GetPlayerRole() : EOutlierPlayerRole::None;
	const UOutlierArenaSubsystem* Arena = PairMarkerArena.Get();
	const uint32 GameplayGeneration = Arena ? Arena->GetGameplayGeneration() : 0;
	if (PairMarkerTarget.Pawn.Get() == Target && PairMarkerTargetPeer.Get() == Peer
		&& PairMarkerSnapshot.PairId == PairId && PairMarkerSnapshot.TargetRole == TargetRole
		&& PairMarkerSnapshot.GameplayGeneration == GameplayGeneration)
	{
		return;
	}

	if (APawn* Previous = PairMarkerTarget.Pawn.Get())
	{
		Previous->OnDestroyed.RemoveDynamic(this, &ThisClass::HandlePairMarkerTargetDestroyed);
	}
	GetWorldTimerManager().ClearTimer(PairMarkerSampleTimer);
	uint32 NextGeneration = PairMarkerSnapshot.TargetGeneration + 1;
	if (NextGeneration == 0)
	{
		NextGeneration = 1;
	}
	PairMarkerSnapshot = {};
	PairMarkerSnapshot.PairId = PairId;
	PairMarkerSnapshot.TargetRole = TargetRole;
	PairMarkerSnapshot.GameplayGeneration = GameplayGeneration;
	PairMarkerSnapshot.TargetGeneration = NextGeneration;
	PairMarkerTarget.Pawn = Target;
	PairMarkerTarget.TargetGeneration = NextGeneration;
	PairMarkerTargetPeer = Peer;
	BindPairMarkerDamageSource(Target);
	if (Target)
	{
		Target->OnDestroyed.AddUniqueDynamic(this, &ThisClass::HandlePairMarkerTargetDestroyed);
		SamplePairMarkerPosition();
		GetWorldTimerManager().SetTimer(PairMarkerSampleTimer, this, &ThisClass::SamplePairMarkerPosition, 0.2f, true);
	}
	else
	{
		RefreshPairMarkerPresentation();
	}
	// 교체와 무효화는 위치 샘플 주기를 기다리지 않는다.
	ForceNetUpdate();
}

void AFirstPersonPlayerController::BindPairMarkerDamageSource(APawn* Target)
{
	if (UOutlierAbilitySystemComponent* Previous = PairMarkerDamageSource.Get())
	{
		Previous->OnDamageApplied.Remove(PairMarkerDamageHandle);
	}
	PairMarkerDamageSource.Reset();
	PairMarkerDamageHandle.Reset();

	// 위치 마커가 가리키는 본체의 ASC에만 연결한다. 빙의로 조종 Pawn이 바뀌어도 이 연결은 유지된다.
	const IAbilitySystemInterface* Source = Cast<IAbilitySystemInterface>(Target);
	if (UOutlierAbilitySystemComponent* ASC =
		Source ? Cast<UOutlierAbilitySystemComponent>(Source->GetAbilitySystemComponent()) : nullptr)
	{
		PairMarkerDamageSource = ASC;
		PairMarkerDamageHandle = ASC->OnDamageApplied.AddUObject(this, &ThisClass::HandlePairMarkerDamageApplied);
	}
}

void AFirstPersonPlayerController::HandlePairMarkerDamageApplied()
{
	const UOutlierAbilitySystemComponent* Source = PairMarkerDamageSource.Get();
	const APawn* Target = PairMarkerTarget.Pawn.Get();
	const AOutlierPlayerState* State = PairMarkerPlayerState.Get();
	const AOutlierPlayerState* Peer = PairMarkerTargetPeer.Get();
	if (!HasAuthority() || bPairMarkerReloading || !IsValid(Target) || !Source || Source->GetAvatarActor() != Target
		|| !State || !Peer || !Peer->GetPlayerController() || Peer->GetPairId() != PairMarkerSnapshot.PairId
		|| !PairMarkerSnapshot.IsFor(State->GetPairId(), State->GetPlayerRole(), PairMarkerSnapshot.GameplayGeneration))
	{
		return;
	}

	if (++PairMarkerHitSequence == 0)
	{
		++PairMarkerHitSequence;
	}
	FPairPositionMarkerHit Hit;
	Hit.PairId = PairMarkerSnapshot.PairId;
	Hit.TargetRole = PairMarkerSnapshot.TargetRole;
	Hit.GameplayGeneration = PairMarkerSnapshot.GameplayGeneration;
	Hit.TargetGeneration = PairMarkerSnapshot.TargetGeneration;
	Hit.Sequence = PairMarkerHitSequence;
	Hit.HitServerTime = GetWorld()->GetTimeSeconds();
	ClientShowPairMarkerHit(Hit);
}

void AFirstPersonPlayerController::ClientShowPairMarkerHit_Implementation(const FPairPositionMarkerHit& Hit)
{
	if (!CanPresentPairMarker())
	{
		UpdatePairMarkerDamageAvailability(false);
		return;
	}
	UpdatePairMarkerDamageAvailability(true);
	const AGameStateBase* GameState = GetWorld()->GetGameState();
	if (GameState)
	{
		const double Duration = PairMarkerWidgetClass.GetDefaultObject()->DamagePulseDuration;
		if (QueuePairMarkerHit(Hit, GameState->GetServerWorldTimeSeconds(), Duration))
		{
			RefreshPairMarkerPresentation();
		}
	}
}

void AFirstPersonPlayerController::UpdatePairMarkerDamageAvailability(bool bAvailable)
{
	if (bAvailable == bPairMarkerDamageHudAvailable)
	{
		return;
	}
	bPairMarkerDamageHudAvailable = bAvailable;
	PendingPairMarkerHit = {};
	if (const AGameStateBase* GameState = GetWorld()->GetGameState())
	{
		// HUD 시작이나 재표시보다 앞선 피격이 늦게 도착해도 다시 재생하지 않는다.
		PairMarkerDamageAcceptAfter = GameState->GetServerWorldTimeSeconds();
	}
}

bool AFirstPersonPlayerController::QueuePairMarkerHit(const FPairPositionMarkerHit& Hit, double ServerTime, double Duration)
{
	if (Hit.PairId == INDEX_NONE || Hit.TargetRole == EOutlierPlayerRole::None || Hit.TargetGeneration == 0
		|| Hit.Sequence == 0 || (LastPairMarkerHitSequence != 0
			&& static_cast<int32>(Hit.Sequence - LastPairMarkerHitSequence) <= 0)
		|| Hit.HitServerTime < PairMarkerDamageAcceptAfter
		|| !FMath::IsFinite(Duration) || Duration <= 0.0 || Hit.GetRemainingSeconds(ServerTime, Duration) <= 0.0)
	{
		return false;
	}
	LastPairMarkerHitSequence = Hit.Sequence;

	if (PairMarkerSnapshot.TargetGeneration != 0
		&& static_cast<int32>(Hit.TargetGeneration - PairMarkerSnapshot.TargetGeneration) < 0)
	{
		return false;
	}
	FVector Position;
	if (Hit.TargetGeneration == PairMarkerSnapshot.TargetGeneration
		&& (!Hit.Matches(PairMarkerSnapshot)
			|| (PresentedPairMarkerGeneration == Hit.TargetGeneration && !ResolvePairMarkerPosition(Position))))
	{
		return false;
	}
	// 위치 상태보다 먼저 받은 피격은 다음 RepNotify가 세대와 남은 시간을 확인할 때까지 보관한다.
	PendingPairMarkerHit = Hit;
	return true;
}

void AFirstPersonPlayerController::ApplyPendingPairMarkerHit()
{
	if (PendingPairMarkerHit.Sequence == 0)
	{
		return;
	}
	const AGameStateBase* GameState = GetWorld()->GetGameState();
	if (!GameState)
	{
		return;
	}
	const double Remaining = PendingPairMarkerHit.GetRemainingSeconds(
		GameState->GetServerWorldTimeSeconds(), PairMarkerWidget->DamagePulseDuration);
	if (Remaining <= 0.0 || static_cast<int32>(PendingPairMarkerHit.TargetGeneration - PairMarkerSnapshot.TargetGeneration) < 0)
	{
		PendingPairMarkerHit = {};
		return;
	}
	if (PendingPairMarkerHit.Matches(PairMarkerSnapshot))
	{
		PendingPairMarkerHit = {};
		PairMarkerWidget->PlayDamagePulse(static_cast<float>(Remaining));
	}
}

void AFirstPersonPlayerController::SamplePairMarkerPosition()
{
	AFirstPersonCharacter* Target = PairMarkerTarget.Pawn.Get();
	const AOutlierPlayerState* Peer = PairMarkerTargetPeer.Get();
	if (!IsValid(Target) || Target->IsActorBeingDestroyed() || !Peer || !Peer->GetPlayerController())
	{
		SetPairMarkerTarget(nullptr, nullptr);
		return;
	}
	PairMarkerSnapshot.Position = Target->GetPairMarkerAnchorLocation();
	PairMarkerSnapshot.SampleServerTime = GetWorld()->GetTimeSeconds();
	// Listen 호스트는 RepNotify가 실행되지 않으므로 같은 표시 경로로 갱신한다.
	RefreshPairMarkerPresentation();
}

void AFirstPersonPlayerController::OnRep_PairMarkerState()
{
	RefreshPairMarkerPresentation();
}

bool AFirstPersonPlayerController::ResolvePairMarkerPosition(FVector& OutPosition) const
{
	const AOutlierPlayerState* State = PairMarkerPlayerState.Get();
	const UOutlierArenaSubsystem* Arena = PairMarkerArena.Get();
	if (!State || bPairMarkerReloading || !PairMarkerSnapshot.IsFor(State->GetPairId(), State->GetPlayerRole(),
		Arena ? Arena->GetGameplayGeneration() : 0))
	{
		return false;
	}

	const AFirstPersonCharacter* TargetPawn = PairMarkerTarget.Pawn.Get();
	if (PairMarkerTarget.Matches(PairMarkerSnapshot) && IsValid(TargetPawn) && !TargetPawn->IsActorBeingDestroyed())
	{
		OutPosition = TargetPawn->GetPairMarkerAnchorLocation();
		return true;
	}

	// 다른 세대의 Pawn 참조나 아직 해석되지 않은 참조는 현 세대의 위치 샘플로 대체한다.
	const AGameStateBase* GameState = GetWorld()->GetGameState();
	if (GameState && PairMarkerSnapshot.HasFreshPosition(GameState->GetServerWorldTimeSeconds()))
	{
		OutPosition = PairMarkerSnapshot.Position;
		return true;
	}
	return false;
}

bool AFirstPersonPlayerController::CanPresentPairMarker() const
{
	const ULocalPlayerUILayerSubsystem* LayerSubsystem = PairMarkerLayers.Get();
	if (!PairMarkerWidgetClass || !LayerSubsystem || LayerSubsystem->GetLocalPlayer()->PlayerController != this
		|| !LayerSubsystem->IsMainUIRegistered(ShooterUIInstance) || LayerSubsystem->IsEndingPresentationActive()
		|| bHudHiddenForDeath || CameraBlackoutReasons != 0 || bPairMarkerReloading)
	{
		return false;
	}

	// 시퀀스 재생이 끝나도 서버가 인트로를 해제할 때까지는 카메라와 입력이 복구 중이다.
	const UOutlierCinematicSubsystem* Cinematic = LayerSubsystem->GetLocalPlayer()->GetSubsystem<UOutlierCinematicSubsystem>();
	return !Cinematic || Cinematic->GetState() == EOutlierCinematicState::Idle
		|| Cinematic->GetState() == EOutlierCinematicState::Failed;
}

bool AFirstPersonPlayerController::IsPairMarkerTargetVisible() const
{
	const AFirstPersonCharacter* Target = PairMarkerTarget.Pawn.Get();
	const AActor* ViewTarget = GetViewTarget();
	if (!PairMarkerTarget.Matches(PairMarkerSnapshot) || !IsValid(Target) || Target->IsActorBeingDestroyed()
		|| Target->IsHidden() || Target == ViewTarget)
	{
		return false;
	}

	const USkeletalMeshComponent* BodyMesh = Target->GetMesh();
	const ULocalPlayer* LocalPlayer = GetLocalPlayer();
	FSceneViewProjectionData View;
	if (!BodyMesh || !BodyMesh->GetSkeletalMeshAsset() || !BodyMesh->ShouldRender()
		|| !LocalPlayer || !LocalPlayer->ViewportClient
		|| !LocalPlayer->GetProjectionData(LocalPlayer->ViewportClient->Viewport, View))
	{
		return false;
	}

	const FMatrix ViewProjection = View.ComputeViewProjectionMatrix();
	FCollisionQueryParams Query(SCENE_QUERY_STAT(PairMarkerVisibility), true);
	// Partner 조명처럼 카메라를 따라가는 부착물이 본체나 벽의 가림으로 잡히지 않게 한다.
	const auto IgnoreCameraActor = [&Query](const AActor* Actor)
	{
		if (Actor)
		{
			Query.AddIgnoredActor(Actor);
			TArray<AActor*> AttachedActors;
			Actor->GetAllChildActors(AttachedActors, true);
			Actor->GetAttachedActors(AttachedActors, false, true);
			Query.AddIgnoredActors(AttachedActors);
		}
	};
	const APawn* ViewerPawn = GetPawn();
	IgnoreCameraActor(ViewerPawn);
	if (ViewTarget != ViewerPawn)
	{
		IgnoreCameraActor(ViewTarget);
	}

	// 머리 위 마커 앵커 대신 몸체를 검사한다. 엄폐물 위로 상체만 보여도 본체를 찾을 수 있다.
	const FVector Center = BodyMesh->Bounds.Origin;
	const FVector UpperBody = Center + FVector(0.0, 0.0, BodyMesh->Bounds.BoxExtent.Z * 0.6);
	for (const FVector& Point : {Center, UpperBody})
	{
		const FVector4 Clip = ViewProjection.TransformFVector4(FVector4(Point, 1.0));
		if (Clip.W <= 0.0 || Clip.Z < 0.0 || Clip.Z > Clip.W
			|| FMath::Abs(Clip.X) > Clip.W || FMath::Abs(Clip.Y) > Clip.W)
		{
			continue;
		}

		FHitResult Hit;
		if (!GetWorld()->LineTraceSingleByChannel(Hit, View.ViewOrigin, Point, ECC_Visibility, Query)
			|| Hit.GetActor() == Target)
		{
			return true;
		}
	}
	return false;
}

void AFirstPersonPlayerController::RefreshPairMarkerPresentation()
{
	const bool bCanPresent = CanPresentPairMarker();
	UpdatePairMarkerDamageAvailability(bCanPresent);
	FVector Position;
	if (!bCanPresent || !ResolvePairMarkerPosition(Position))
	{
		ReleasePairMarkerHud();
		return;
	}
	if (!PairMarkerHud)
	{
		PairMarkerHud = UTargetPointHudWidget::CreateForPlayer(this, this);
		if (!PairMarkerHud)
		{
			return;
		}
		PairMarkerWidget = CreateWidget<UPairPositionMarkerWidget>(this, PairMarkerWidgetClass);
		if (!PairMarkerWidget)
		{
			ReleasePairMarkerHud();
			return;
		}
		PairMarkerHud->AddContent(PairMarkerWidget);
	}
	if (PresentedPairMarkerGeneration != PairMarkerSnapshot.TargetGeneration)
	{
		PairMarkerWidget->StopDamagePulse();
		PresentedPairMarkerGeneration = PairMarkerSnapshot.TargetGeneration;
	}
	PairMarkerWidget->SetTargetRole(PairMarkerSnapshot.TargetRole);
	ApplyPendingPairMarkerHit();
}

void AFirstPersonPlayerController::ReleasePairMarkerHud()
{
	if (PairMarkerHud)
	{
		if (PairMarkerWidget)
		{
			PairMarkerWidget->StopDamagePulse();
		}
		PairMarkerHud->Release();
		PairMarkerHud = nullptr;
		PairMarkerWidget = nullptr;
	}
}

void AFirstPersonPlayerController::UpdateCameraManager(float DeltaSeconds)
{
	Super::UpdateCameraManager(DeltaSeconds);
	if (PairMarkerHud)
	{
		// 대상이 정지해도 카메라 회전에 따라 화면 위치가 변한다. 활성 마커만 카메라 갱신 뒤 투영한다.
		UpdatePairMarkerPlacement();
	}
}

void AFirstPersonPlayerController::UpdatePairMarkerPlacement()
{
	FVector Position;
	if ((!PairMarkerHud->GetParent() && !CanPresentPairMarker()) || !ResolvePairMarkerPosition(Position))
	{
		ReleasePairMarkerHud();
		return;
	}
	// HUD 레이어가 교체되면 공용 타깃 HUD가 다음 투영에서 기존 마커를 다시 붙인다.
	bool bShowRearIndicator = false;
	PairMarkerHud->UpdateContent(PairMarkerWidget, !IsPairMarkerTargetVisible(), Position, PairMarkerWidget->MarkerSize,
		PairMarkerWidget->HudPresentation, FVector2D(0.5, 1.0), &bShowRearIndicator);
	PairMarkerWidget->SetRearIndicatorVisible(bShowRearIndicator);
	PairMarkerWidget->UpdateDamagePulse();
}
