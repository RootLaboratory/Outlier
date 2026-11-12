#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Drone/Partner/PartnerHackComponent.h"
#include "Components/SceneComponent.h"
#include "Components/TextBlock.h"
#include "Drone/Partner/HackableComponent.h"
#include "Drone/Partner/HackGameplayTags.h"
#include "Drone/Partner/HackTestActor.h"
#include "Drone/Partner/PartnerCharacter.h"
#include "Engine/Engine.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GAS/OutlierAbilitySystemComponent.h"
#include "GameplayTags/OutlierGameplayTags.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "UI/HackCandidateLayerWidget.h"
#include "UI/HackCandidateMarkerWidget.h"
#include "UI/HackableInfoWidget.h"
#include "GameFramework/PlayerController.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPartnerHackHoldTest,
	"Outlier.Hack.Hold.RangeLockAndCancellation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPartnerHackHoldTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false,
		MakeUniqueObjectName(nullptr, UWorld::StaticClass(), TEXT("HackHoldTest")));
	if (!TestNotNull(TEXT("World exists"), World)) return false;
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());
	ON_SCOPE_EXIT
	{
		World->DestroyWorld(false);
		GEngine->DestroyWorldContext(World);
		World->RemoveFromRoot();
	};
	APartnerCharacter* Partner = World->SpawnActor<APartnerCharacter>();
	AHackTestActor* Target = World->SpawnActor<AHackTestActor>();
	if (!TestNotNull(TEXT("Partner exists"), Partner) || !TestNotNull(TEXT("Target exists"), Target)) return false;
	USceneComponent* Root = NewObject<USceneComponent>(Target, TEXT("HoldTestRoot"));
	Target->AddInstanceComponent(Root);
	Target->SetRootComponent(Root);
	Root->RegisterComponent();
	UHackableComponent* Hackable = Target->GetHackableComponent();
	Hackable->HackTags.AddTag(HackGameplayTags::Target::NonPossessable());
	UPartnerHackComponent* Hack = Partner->FindComponentByClass<UPartnerHackComponent>();
	if (!TestNotNull(TEXT("Hack component exists"), Hack)) return false;
	Hack->RefreshCharacterRefsFromPlayerState();
	Hack->bDebugHack = false;
	Hack->BlockedCandidateTags.AddTag(OutlierGameplayTags::State::Locked());
	UOutlierAbilitySystemComponent* ASC = Partner->GetOutlierAbilitySystemComponent();
	ASC->InitializeForPawn(Partner);
	FOutlierPartnerAbilityConfig Config;
	Config.HackEffectiveRange = 500.0f;
	if (!TestTrue(TEXT("Abilities configured"), ASC->ConfigurePartnerAbilities(Config))) return false;
	FPartnerHackAbilityData AbilityData;
	AbilityData.EffectiveRange = 500.0f;
	Hack->CacheAbilityData(AbilityData);
	Hack->bHackCandidateSearchActive = true;
	FVector2D Screen;
	const FVector Origin = Partner->GetActorLocation();
	Target->SetActorLocation(Origin + FVector(200, 0, 0));
	Hack->ServerBeginHackHold(Target, 1);
	TestEqual(TEXT("R press caches an eligible target on server"), Hack->ServerHoldId, uint32(1));

	Target->SetActorLocation(Origin + FVector(-200, 0, 0));
	TestFalse(TEXT("Target behind camera cannot become a new candidate"),
		Hack->IsCandidateActorValid(Target, Hackable, Hack->BuildQueryContext(), Screen));
	TestTrue(TEXT("Cached target behind camera remains valid within range"),
		Hack->CanMaintainHackHold(Target, Hack->BuildQueryContext()));
	Hack->TickComponent(0.016f, LEVELTICK_All, nullptr);
	TestEqual(TEXT("Server retains offscreen lock"), Hack->ServerHoldId, uint32(1));

	// Hover changes and R release operate on the cache independently of a marker's lifetime.
	Hack->HeldHackActor = Target;
	Hack->LocalHoldId = 1;
	Hack->HoveredHackActor = Target;
	Hack->NotifyHackMarkerUnhovered(nullptr, Target);
	TestTrue(TEXT("Unhover preserves cached hold"), Hack->IsHackHoldActive());
	Hack->EndHackHold();
	TestFalse(TEXT("R release clears local cache"), Hack->IsHackHoldActive());
	TestEqual(TEXT("R release clears server cache"), Hack->ServerHoldId, uint32(0));
	Hack->ServerTryStartHack(Target, 1);
	TestFalse(TEXT("Delayed completion after release cannot hack"),
		Hackable->HackTags.HasTag(OutlierGameplayTags::State::HackedOnce()));

	Target->SetActorLocation(Origin + FVector(200, 0, 0));
	Hack->ServerBeginHackHold(Target, 2);
	Target->SetActorLocation(Origin + FVector(501, 0, 0));
	Hack->TickComponent(0.016f, LEVELTICK_All, nullptr);
	TestEqual(TEXT("Range exit invalidates server lock immediately"), Hack->ServerHoldId, uint32(0));
	Target->SetActorLocation(Origin + FVector(200, 0, 0));
	Hack->ServerTryStartHack(Target, 2);
	TestFalse(TEXT("Reentry cannot resume cancelled hold"),
		Hackable->HackTags.HasTag(OutlierGameplayTags::State::HackedOnce()));

	Target->SetActorLocation(Origin + FVector(500, 0, 0));
	TestTrue(TEXT("Exact range boundary is permitted"), Hack->CanMaintainHackHold(Target, Hack->BuildQueryContext()));
	Hackable->HackTags.AddTag(OutlierGameplayTags::State::Locked());
	TestFalse(TEXT("Locked state still blocks a cached target"), Hack->CanMaintainHackHold(Target, Hack->BuildQueryContext()));
	Hackable->HackTags.RemoveTag(OutlierGameplayTags::State::Locked());
	TestFalse(TEXT("Destroyed/missing target cannot maintain a hold"), Hack->CanMaintainHackHold(nullptr, Hack->BuildQueryContext()));

	Target->SetActorLocation(Origin + FVector(200, 0, 0));
	Hack->ServerBeginHackHold(Target, 3);
	Hack->ServerEndHackHold(2);
	TestEqual(TEXT("Old cancellation cannot cancel a newer hold"), Hack->ServerHoldId, uint32(3));
	Target->SetActorLocation(Origin + FVector(-200, 0, 0));
	Hack->ServerTryStartHack(Target, 3);
	TestTrue(TEXT("Offscreen locked hold completes on server"),
		Hackable->HackTags.HasTag(OutlierGameplayTags::State::HackedOnce()));
	TestEqual(TEXT("Completion consumes the cached hold"), Hack->ServerHoldId, uint32(0));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPartnerHackHoldWidgetTest,
	"Outlier.Hack.Hold.InfoSurvivesMarkerRemoval",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPartnerHackHoldWidgetTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	if (!TestNotNull(TEXT("UI world exists"), World)) return false;
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());
	ON_SCOPE_EXIT
	{
		World->DestroyWorld(false);
		GEngine->DestroyWorldContext(World);
		World->RemoveFromRoot();
	};
	APlayerController* PC = World->SpawnActor<APlayerController>();
	ULocalPlayer* Player = NewObject<ULocalPlayer>(GEngine);
	PC->Player = Player;
	Player->PlayerController = PC;
	PC->SetAsLocalPlayerController();
	ON_SCOPE_EXIT { PC->Player = nullptr; Player->PlayerController = nullptr; };
	AHackTestActor* Target = World->SpawnActor<AHackTestActor>();
	APartnerCharacter* Partner = World->SpawnActor<APartnerCharacter>();
	UPartnerHackComponent* Hack = Partner->FindComponentByClass<UPartnerHackComponent>();
	UHackCandidateLayerWidget* Layer = CreateWidget<UHackCandidateLayerWidget>(PC);
	if (!TestNotNull(TEXT("Layer created"), Layer)) return false;
	Layer->BindHackComponent(Hack);
	const TSharedRef<SWidget> SlateLayer = Layer->TakeWidget();
	Layer->HackCandidateNull = NewObject<UTextBlock>(Layer);
	Layer->AddCandidate(Target, Target->GetHackableComponent());
	UHackCandidateMarkerWidget* Marker = Layer->MarkerWidgets.FindRef(Target);
	if (!TestNotNull(TEXT("Candidate marker created"), Marker)) return false;
	UHackableInfoWidget* Info = Marker->ReleaseInfoForHackHold();
	if (!TestNotNull(TEXT("Hover info transferred"), Info)) return false;
	Layer->BeginHackHold(Target, Info);
	Layer->RemoveCandidate(Target, Target->GetHackableComponent());
	TestTrue(TEXT("Selected marker disappears"), Layer->MarkerWidgets.IsEmpty());
	TestTrue(TEXT("Held info stays on the canvas after marker destruction"), Info->GetParent() != nullptr);
	TestEqual(TEXT("Held info remains visible"), Info->GetVisibility(), ESlateVisibility::HitTestInvisible);
	TestEqual(TEXT("Empty-candidate message stays hidden while holding"),
		Layer->HackCandidateNull->GetVisibility(), ESlateVisibility::Collapsed);
	Layer->ClearMarkers();
	TestTrue(TEXT("Candidate cleanup cannot remove held info"), Info->GetParent() != nullptr);
	Layer->EndHackHold();
	TestNull(TEXT("Cancellation removes info from canvas"), Info->GetParent());
	TestEqual(TEXT("Candidate message returns after cancellation"),
		Layer->HackCandidateNull->GetVisibility(), ESlateVisibility::Visible);
	Layer->BindHackComponent(nullptr);
	return true;
}

#endif
