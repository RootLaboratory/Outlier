#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "UI/PairPositionMarkerWidget.h"

#include "Components/Image.h"
#include "Components/SceneComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "FirstPerson/FirstPersonCharacter.h"
#include "FirstPerson/FirstPersonPlayerController.h"
#include "GameFramework/GameStateBase.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Network/OutlierArenaSubsystem.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPairPositionMarkerLifecycleTest,
	"Outlier.UI.PairPositionMarker.TargetLifecycle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPairPositionMarkerLifecycleTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	if (!TestNotNull(TEXT("Test world"), World))
	{
		return false;
	}
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());
	ON_SCOPE_EXIT
	{
		World->DestroyWorld(false);
		GEngine->DestroyWorldContext(World);
		World->RemoveFromRoot();
	};

	AFirstPersonPlayerController* Controller = World->SpawnActor<AFirstPersonPlayerController>();
	AOutlierPlayerState* State = World->SpawnActor<AOutlierPlayerState>();
	AFirstPersonCharacter* Counterpart = World->SpawnActor<AFirstPersonCharacter>(FVector(200, 0, 0), FRotator::ZeroRotator);
	AGameStateBase* Clock = World->SpawnActor<AGameStateBase>();
	if (!TestNotNull(TEXT("Controller"), Controller) || !TestNotNull(TEXT("State"), State)
		|| !TestNotNull(TEXT("Counterpart"), Counterpart) || !TestNotNull(TEXT("Clock"), Clock))
	{
		return false;
	}
	World->SetGameState(Clock);
	State->SetPlayerRole(EOutlierPlayerRole::Partner);
	State->SetPairId(4);
	Controller->PairMarkerPlayerState = State;
	FPairPositionMarkerSnapshot& Snapshot = Controller->PairMarkerSnapshot;
	Snapshot.PairId = 4;
	Snapshot.TargetRole = EOutlierPlayerRole::Shooter;
	Snapshot.TargetGeneration = 2;
	Snapshot.SampleServerTime = Clock->GetServerWorldTimeSeconds();
	Snapshot.Position = FVector(100, 0, 0);

	FVector Position;
	TestTrue(TEXT("An unresolved pawn uses the current position sample"), Controller->ResolvePairMarkerPosition(Position));
	TestFalse(TEXT("A position sample alone cannot hide the marker"), Controller->IsPairMarkerTargetVisible());
	TestEqual(TEXT("Sampled position"), Position, FVector(100, 0, 0));
	Controller->PairMarkerTarget.Pawn = Counterpart;
	Controller->PairMarkerTarget.TargetGeneration = 1;
	TestTrue(TEXT("A previous pawn generation still uses sampled position"), Controller->ResolvePairMarkerPosition(Position));
	TestFalse(TEXT("A stale pawn generation cannot hide the marker"), Controller->IsPairMarkerTargetVisible());
	TestEqual(TEXT("Previous pawn does not replace the sample"), Position, FVector(100, 0, 0));
	Controller->PairMarkerTarget.TargetGeneration = 2;
	TestTrue(TEXT("The current pawn takes over when its reference resolves"), Controller->ResolvePairMarkerPosition(Position));
	TestEqual(TEXT("Shooter marker uses the default anchor above the capsule"), Position, FVector(200, 0, 116));
	USceneComponent* Anchor = FindObject<USceneComponent>(Counterpart, TEXT("PairMarkerAnchor"));
	if (!TestNotNull(TEXT("Editable marker anchor"), Anchor))
	{
		return false;
	}
	Anchor->SetRelativeLocation(FVector(10, 20, 75));
	TestTrue(TEXT("A Partner sees the configured Shooter anchor before suit acquisition"), Controller->ResolvePairMarkerPosition(Position));
	TestEqual(TEXT("Configured anchor relative to the Shooter body"), Position, FVector(210, 20, 75));
	USkeletalMesh* BodyAsset = NewObject<USkeletalMesh>();
	BodyAsset->SetImportedBounds(FBoxSphereBounds(FBox(FVector(-30, -25, -20), FVector(30, 25, 70))));
	USkeletalMeshComponent* BodyMesh = Counterpart->GetMesh();
	BodyMesh->SetSkeletalMeshAsset(BodyAsset);
	BodyMesh->SetRelativeLocation(FVector(0, 0, -40));
	TestTrue(TEXT("A Partner resolves the automatic anchor from the Shooter body mesh"), Controller->ResolvePairMarkerPosition(Position));
	TestEqual(TEXT("Automatic anchor uses the transformed mesh top plus clearance"), Position, FVector(200, 0, 38));

	Controller->PairMarkerTarget.Pawn = nullptr;
	Snapshot.SampleServerTime -= 2.0;
	TestFalse(TEXT("An expired sample hides the marker"), Controller->ResolvePairMarkerPosition(Position));
	Snapshot.SampleServerTime = Clock->GetServerWorldTimeSeconds();
	TestTrue(TEXT("A fresh sample restores the marker"), Controller->ResolvePairMarkerPosition(Position));
	Snapshot.PairId = 9;
	TestFalse(TEXT("A different pair never displays"), Controller->ResolvePairMarkerPosition(Position));
	Snapshot.PairId = 4;
	Snapshot.GameplayGeneration = 1;
	TestFalse(TEXT("A mismatched gameplay generation never displays"), Controller->ResolvePairMarkerPosition(Position));
	Snapshot.GameplayGeneration = 0;
	State->SetPlayerRole(EOutlierPlayerRole::Shooter);
	TestFalse(TEXT("A Shooter never sees a Shooter marker"), Controller->ResolvePairMarkerPosition(Position));
	Snapshot.TargetRole = EOutlierPlayerRole::Partner;
	TestTrue(TEXT("A Shooter sees the paired Partner"), Controller->ResolvePairMarkerPosition(Position));

	UPairPositionMarkerWidget* Widget = NewObject<UPairPositionMarkerWidget>();
	Widget->RoleIcon = NewObject<UImage>(Widget);
	Widget->ShooterIcon = UTexture2D::CreateTransient(32, 32);
	Widget->PartnerIcon = UTexture2D::CreateTransient(40, 32);
	Widget->RoleIcon->SetBrushFromTexture(Widget->ShooterIcon);
	Widget->RoleIcon->SetBrushSize(FVector2D(8, 8));
	Widget->SetTargetRole(EOutlierPlayerRole::Shooter);
	TestEqual(TEXT("Shooter uses the configured texture"),
		Widget->RoleIcon->GetBrush().GetResourceObject(), static_cast<UObject*>(Widget->ShooterIcon.Get()));
	TestEqual(TEXT("Shooter brush uses the original square dimensions"),
		FVector2D(Widget->RoleIcon->GetBrush().GetImageSize()), FVector2D(32, 32));
	Widget->SetTargetRole(EOutlierPlayerRole::Partner);
	TestEqual(TEXT("Partner uses the configured texture"),
		Widget->RoleIcon->GetBrush().GetResourceObject(), static_cast<UObject*>(Widget->PartnerIcon.Get()));
	TestEqual(TEXT("Partner brush uses the original non-square dimensions"),
		FVector2D(Widget->RoleIcon->GetBrush().GetImageSize()), FVector2D(40, 32));
	TestTrue(TEXT("The marker keeps targets behind the camera visible"), Widget->HudPresentation.bKeepBehindCamera);
	TestNull(TEXT("A legacy widget can omit the rear indicator"), Widget->RearIndicator.Get());
	Widget->SetRearIndicatorVisible(true);
	Widget->RearIndicator = NewObject<UImage>(Widget);
	Widget->SetRearIndicatorVisible(true);
	TestEqual(TEXT("The bound rear indicator shows without taking input"),
		Widget->RearIndicator->GetVisibility(), ESlateVisibility::HitTestInvisible);
	Widget->SetRearIndicatorVisible(false);
	TestEqual(TEXT("The bound rear indicator is collapsed when cleared"),
		Widget->RearIndicator->GetVisibility(), ESlateVisibility::Collapsed);

	UOutlierArenaSubsystem* Arena = World->GetSubsystem<UOutlierArenaSubsystem>();
	if (!TestNotNull(TEXT("Arena subsystem"), Arena))
	{
		return false;
	}
	Controller->BindPairMarkerSources(State);
	for (uint32 Generation = 1; Generation <= 2; ++Generation)
	{
		Controller->PendingGameplayGeneration = Generation;
		Controller->bHasPendingArenaRequest = true;
		Controller->ContinueClientGameplayReload();
		TestTrue(TEXT("Client reload registers its own Ready callback"),
			Controller->ClientArenaGameplayReadyHandle.IsValid());

		// 임시 월드에는 스트리밍 레벨이 없으므로 레벨 준비 완료 이벤트를 직접 전달한다.
		Controller->bClientReloadBootstrapPending = false;
		Arena->OnArenaGameplayReloadStarted.Broadcast(Generation);
		TestTrue(TEXT("Reload start suspends the pair marker"), Controller->bPairMarkerReloading);
		Arena->OnArenaGameplayReady.Broadcast(Generation);
		TestFalse(TEXT("Arena Ready resumes the pair marker"), Controller->bPairMarkerReloading);
		TestFalse(TEXT("Client Ready removes its own callback"),
			Controller->ClientArenaGameplayReadyHandle.IsValid());
	}
	Controller->ClearClientArenaContentWait();
	Controller->ClearPairMarkerSources();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPairPositionMarkerDamageTest,
	"Outlier.UI.PairPositionMarker.DamagePulse",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPairPositionMarkerDamageTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	if (!TestNotNull(TEXT("Test world"), World))
	{
		return false;
	}
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());
	ON_SCOPE_EXIT
	{
		World->DestroyWorld(false);
		GEngine->DestroyWorldContext(World);
		World->RemoveFromRoot();
	};

	AFirstPersonPlayerController* Controller = World->SpawnActor<AFirstPersonPlayerController>();
	AGameStateBase* Clock = World->SpawnActor<AGameStateBase>();
	if (!TestNotNull(TEXT("Controller"), Controller) || !TestNotNull(TEXT("Clock"), Clock))
	{
		return false;
	}
	World->SetGameState(Clock);
	UPairPositionMarkerWidget* Widget = NewObject<UPairPositionMarkerWidget>(World);
	Controller->PairMarkerWidget = Widget;
	Widget->PlayDamagePulse(0.6f);
	TestFalse(TEXT("An unbound role icon does not start a timer"),
		World->GetTimerManager().TimerExists(Widget->DamagePulseTimer));
	Widget->RoleIcon = NewObject<UImage>(Widget);
	const FLinearColor OriginalTint(0.8f, 0.9f, 1.0f, 0.65f);
	Widget->RoleIcon->SetColorAndOpacity(OriginalTint);

	const double Now = Clock->GetServerWorldTimeSeconds();
	const double Duration = Widget->DamagePulseDuration;
	Controller->PairMarkerDamageAcceptAfter = Now - 1.0;
	FPairPositionMarkerSnapshot& Snapshot = Controller->PairMarkerSnapshot;
	Snapshot.PairId = 4;
	Snapshot.TargetRole = EOutlierPlayerRole::Partner;
	Snapshot.TargetGeneration = 1;
	FPairPositionMarkerHit Hit;
	Hit.PairId = Snapshot.PairId;
	Hit.TargetRole = Snapshot.TargetRole;
	Hit.TargetGeneration = 2;
	Hit.Sequence = 1;
	Hit.HitServerTime = Now - 0.2;

	TestTrue(TEXT("A fresh hit can arrive before the new position generation"),
		Controller->QueuePairMarkerHit(Hit, Now, Duration));
	Controller->ApplyPendingPairMarkerHit();
	TestFalse(TEXT("The old target does not show the new target's hit"), Widget->OriginalRoleIconTint.IsSet());
	Snapshot.TargetGeneration = Hit.TargetGeneration;
	Controller->ApplyPendingPairMarkerHit();
	if (!TestTrue(TEXT("Matching position starts the pulse without a pawn reference"), Widget->OriginalRoleIconTint.IsSet()))
	{
		return false;
	}
	const FLinearColor PeakTint(1.0f, 0.0f, 0.0f, OriginalTint.A);
	TestTrue(TEXT("The role icon turns red immediately and preserves alpha"),
		Widget->RoleIcon->GetColorAndOpacity().Equals(PeakTint));
	Widget->ApplyDamagePulseTint(0.5f);
	const FLinearColor MidTint = FMath::Lerp(OriginalTint, PeakTint, 0.5f);
	TestTrue(TEXT("The role icon fades toward its original color"),
		Widget->RoleIcon->GetColorAndOpacity().Equals(MidTint));
	TestTrue(TEXT("A late hit plays for the remaining time"),
		FMath::IsNearlyEqual(World->GetTimerManager().GetTimerRemaining(Widget->DamagePulseTimer), 0.4f, KINDA_SMALL_NUMBER));
	TestFalse(TEXT("The same hit sequence is ignored"), Controller->QueuePairMarkerHit(Hit, Now, Duration));

	++Hit.Sequence;
	Hit.HitServerTime = Now;
	TestTrue(TEXT("A second hit is accepted"), Controller->QueuePairMarkerHit(Hit, Now, Duration));
	Controller->ApplyPendingPairMarkerHit();
	TestTrue(TEXT("Extending the fade preserves the current tint"), Widget->RoleIcon->GetColorAndOpacity().Equals(MidTint));
	Widget->UpdateDamagePulse();
	TestTrue(TEXT("The next update continues from the same tint"), Widget->RoleIcon->GetColorAndOpacity().Equals(MidTint));
	TestTrue(TEXT("The second hit extends the timer"),
		FMath::IsNearlyEqual(World->GetTimerManager().GetTimerRemaining(Widget->DamagePulseTimer), 0.6f));
	Widget->StopDamagePulse();
	TestFalse(TEXT("Stopping disables playback and removes the timer"),
		Widget->OriginalRoleIconTint.IsSet() || World->GetTimerManager().TimerExists(Widget->DamagePulseTimer));
	TestEqual(TEXT("Stopping restores the configured icon tint"), Widget->RoleIcon->GetColorAndOpacity(), OriginalTint);
	Widget->PlayDamagePulse(0.6f);
	Widget->UpdateDamagePulse();
	TestTrue(TEXT("The next pulse starts in red again"), Widget->RoleIcon->GetColorAndOpacity().Equals(PeakTint));
	Widget->StopDamagePulse();

	++Hit.Sequence;
	Hit.HitServerTime = Now - Duration;
	TestFalse(TEXT("An expired hit is ignored"), Controller->QueuePairMarkerHit(Hit, Now, Duration));
	Hit.HitServerTime = Now;
	Hit.TargetGeneration = 1;
	TestFalse(TEXT("A hit from the old target is ignored"), Controller->QueuePairMarkerHit(Hit, Now, Duration));
	++Hit.Sequence;
	Hit.TargetGeneration = Snapshot.TargetGeneration;
	Hit.PairId = 7;
	TestFalse(TEXT("A hit from a different pair cannot play on the current target"),
		Controller->QueuePairMarkerHit(Hit, Now, Duration));
	++Hit.Sequence;
	Hit.PairId = Snapshot.PairId;
	Controller->UpdatePairMarkerDamageAvailability(true);
	Hit.HitServerTime = Now - 0.1;
	TestFalse(TEXT("A hit from before HUD availability is ignored"),
		Controller->QueuePairMarkerHit(Hit, Now, Duration));
	return true;
}

#endif
