#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Interaction/InteractableComponent.h"

#include "Components/SceneComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Interaction/InteractionDescActor.h"
#include "Interaction/InteractableSwitchBase.h"
#include "Interaction/InteractionStatMachine.h"
#include "Interaction/InteractionNode.h"
#include "Interaction/JournalInteraction.h"
#include "Interaction/SuitInteraction.h"
#include "Weapon/WeaponBase.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FInteractionTargetPointTest,
	"Outlier.Interaction.TargetPoint.ReferenceAndPresentation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FInteractionTargetPointTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	if (!TestNotNull(TEXT("Test world exists"), World)) return false;
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());
	ON_SCOPE_EXIT
	{
		World->DestroyWorld(false);
		GEngine->DestroyWorldContext(World);
		World->RemoveFromRoot();
	};
	AActor* Owner = World->SpawnActor<AActor>();
	AActor* OtherOwner = World->SpawnActor<AActor>();
	const auto AddPoint = [](AActor* Actor, FName Name, USceneComponent* Parent)
	{
		USceneComponent* Point = NewObject<USceneComponent>(Actor, Name);
		Actor->AddInstanceComponent(Point);
		if (Parent) Point->SetupAttachment(Parent);
		else Actor->SetRootComponent(Point);
		Point->RegisterComponent();
		return Point;
	};
	USceneComponent* Root = AddPoint(Owner, TEXT("Root"), nullptr);
	USceneComponent* Point = AddPoint(Owner, TEXT("CustomPoint"), Root);
	USceneComponent* ForeignPoint = AddPoint(OtherOwner, TEXT("ForeignPoint"), nullptr);
	Owner->SetActorLocation(FVector(100, 200, 300));
	Point->SetRelativeLocation(FVector(40, 50, 60));
	UInteractableComponent* Interactable = NewObject<UInteractableComponent>(Owner);
	Owner->AddInstanceComponent(Interactable);
	Interactable->RegisterComponent();
	TestEqual(TEXT("Empty selection uses actor origin without height offset"),
		Interactable->GetInteractionWidgetLocation(), Owner->GetActorLocation());
	TestFalse(TEXT("Inactive UI does not tick"), Interactable->IsComponentTickEnabled());

	Interactable->InteractionTargetPointReference.PathToComponent = Point->GetName();
	Interactable->RefreshInteractionTargetPoint();
	TestEqual(TEXT("Explicit point is respected without offset"),
		Interactable->GetInteractionWidgetLocation(), Point->GetComponentLocation());
	Interactable->InteractionDescActorClass = AInteractionDescActor::StaticClass();
	AInteractionDescActor* Desc = Interactable->EnsureInteractionDescActor();
	if (TestNotNull(TEXT("BP compatible description facade exists"), Desc))
	{
		TestEqual(TEXT("Description source uses the same point"), Desc->GetActorLocation(), Point->GetComponentLocation());
		TestFalse(TEXT("Inactive description does not tick"), Desc->IsActorTickEnabled());
	}
	// 화면 뒤로 숨겨진 상태에서도 표시 요청이 살아 있으면 카메라 투영을 계속 갱신한다.
	Interactable->bKeyShown = true;
	Interactable->UpdateWidgetPresentation();
	TestTrue(TEXT("Logical visibility keeps projection ticking"), Interactable->IsComponentTickEnabled());
	Interactable->bKeyShown = false;
	Interactable->UpdateWidgetPresentation();
	TestFalse(TEXT("Hidden UI stops ticking"), Interactable->IsComponentTickEnabled());

	Interactable->InteractionTargetPointReference.PathToComponent = TEXT("MissingPoint");
	Interactable->RefreshInteractionTargetPoint();
	TestEqual(TEXT("Missing point falls back to origin"), Interactable->GetInteractionWidgetLocation(), Owner->GetActorLocation());
	Interactable->InteractionTargetPointReference = FComponentReference();
	Interactable->InteractionTargetPointReference.OverrideComponent = ForeignPoint;
	Interactable->RefreshInteractionTargetPoint();
	TestEqual(TEXT("Foreign point is rejected"), Interactable->GetInteractionWidgetLocation(), Owner->GetActorLocation());
	Interactable->InteractionTargetPointReference.OverrideComponent = Point;
	Interactable->RefreshInteractionTargetPoint();
	Point->DestroyComponent();
	TestEqual(TEXT("Destroyed point safely falls back"), Interactable->GetInteractionWidgetLocation(), Owner->GetActorLocation());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FInteractionTargetPointDefaultsTest,
	"Outlier.Interaction.TargetPoint.NativeDefaults",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FInteractionTargetPointDefaultsTest::RunTest(const FString& Parameters)
{
	const TArray<UClass*> Classes = { AInteractableSwitchBase::StaticClass(), AInteractionStatMachine::StaticClass(),
		AInteractionNode::StaticClass(), AJournalInteraction::StaticClass(), ASuitInteraction::StaticClass(), AWeaponBase::StaticClass() };
	for (UClass* Class : Classes)
	{
		AActor* Defaults = CastChecked<AActor>(Class->GetDefaultObject());
		USceneComponent* Point = Cast<USceneComponent>(Defaults->GetDefaultSubobjectByName(TEXT("InteractionTargetPoint")));
		UInteractableComponent* Interactable = Cast<UInteractableComponent>(Defaults->GetDefaultSubobjectByName(TEXT("InteractableComponent")));
		if (!TestNotNull(Class->GetName() + TEXT(" point"), Point)
			|| !TestNotNull(Class->GetName() + TEXT(" interactable"), Interactable)) continue;
		TestTrue(Class->GetName() + TEXT(" editable inherited point"), Point->bEditableWhenInherited);
		TestEqual(Class->GetName() + TEXT(" preserves existing root"), Point->GetAttachParent(), Defaults->GetRootComponent());
		TestEqual(Class->GetName() + TEXT(" default connection"), Interactable->InteractionTargetPointReference.GetComponent(Defaults), static_cast<UActorComponent*>(Point));
		TestEqual(Class->GetName() + TEXT(" no position offset"), Point->GetRelativeLocation(), FVector::ZeroVector);
	}
	return true;
}
#endif
