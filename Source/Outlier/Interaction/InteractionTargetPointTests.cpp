#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Interaction/InteractableComponent.h"

#include "Components/SceneComponent.h"
#include "Components/WidgetComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Interaction/InteractionDescActor.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FInteractionTargetPointTest,
	"Outlier.Interaction.TargetPoint.ReferenceAndPresentation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FInteractionTargetPointTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false,
		MakeUniqueObjectName(nullptr, UWorld::StaticClass(), TEXT("InteractionTargetPointTest")));
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
	if (!TestNotNull(TEXT("Owner exists"), Owner)
		|| !TestNotNull(TEXT("Other owner exists"), OtherOwner)) return false;
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
	// 해킹용 기준점을 그대로 선택할 수 있으며 전용 컴포넌트 이름을 강제하지 않는다.
	USceneComponent* Point = AddPoint(Owner, TEXT("HackTargetPoint"), Root);
	USceneComponent* ForeignPoint = AddPoint(OtherOwner, TEXT("ForeignPoint"), nullptr);
	Owner->SetActorLocation(FVector(100, 200, 300));
	Point->SetRelativeLocation(FVector(40, 50, 60));
	UInteractableComponent* Interactable = NewObject<UInteractableComponent>(Owner);
	Owner->AddInstanceComponent(Interactable);
	Interactable->RegisterComponent();
	const FVector LegacyLocation = Owner->GetActorLocation() + FVector(0, 0, 120);
	TestEqual(TEXT("Empty selection preserves legacy height"),
		Interactable->GetInteractionWidgetLocation(120), LegacyLocation);
	TestFalse(TEXT("Point support does not enable inactive tick"), Interactable->IsComponentTickEnabled());

	Interactable->InteractionTargetPointReference.PathToComponent = Point->GetName();
	Interactable->RefreshInteractionTargetPoint();
	TestEqual(TEXT("Selected point replaces legacy height"),
		Interactable->GetInteractionWidgetLocation(120), Point->GetComponentLocation());

	// 실제 네 가지 화면 UI 갱신 경로에서 기준점 + 개별 WidgetOffset만 적용되는지 확인한다.
	const auto AddWidget = [Owner, Root]()
	{
		UWidgetComponent* Widget = NewObject<UWidgetComponent>(Owner);
		Owner->AddInstanceComponent(Widget);
		Widget->SetupAttachment(Root);
		Widget->RegisterComponent();
		Widget->SetVisibility(true);
		return Widget;
	};
	Interactable->InteractInfoWidgetComponent = AddWidget();
	Interactable->InteractKeyWidgetComponent = AddWidget();
	Interactable->InteractionLockedWidgetComponent = AddWidget();
	Interactable->InteractionOutlineWidgetComponent = AddWidget();
	Interactable->InteractInfoWidgetPresentation.WidgetOffset = FVector(1, 2, 3);
	Interactable->InteractKeyWidgetPresentation.WidgetOffset = FVector(4, 5, 6);
	Interactable->InteractionLockedWidgetPresentation.WidgetOffset = FVector(7, 8, 9);
	Interactable->InteractionOutlineWidgetPresentation.WidgetOffset = FVector(10, 11, 12);
	Interactable->UpdateWidgetPresentation();
	const FVector Anchor = Point->GetComponentLocation();
	TestEqual(TEXT("Info uses point and its offset"),
		Interactable->InteractInfoWidgetComponent->GetComponentLocation(), Anchor + FVector(1, 2, 3));
	TestEqual(TEXT("Key uses point and its offset"),
		Interactable->InteractKeyWidgetComponent->GetComponentLocation(), Anchor + FVector(4, 5, 6));
	TestEqual(TEXT("Locked uses point and its offset"),
		Interactable->InteractionLockedWidgetComponent->GetComponentLocation(), Anchor + FVector(7, 8, 9));
	TestEqual(TEXT("Outline uses point and its offset"),
		Interactable->InteractionOutlineWidgetComponent->GetComponentLocation(), Anchor + FVector(10, 11, 12));
	Interactable->InteractionDescActorClass = AInteractionDescActor::StaticClass();
	AInteractionDescActor* Desc = Interactable->EnsureInteractionDescActor();
	if (TestNotNull(TEXT("Description actor exists"), Desc))
	{
		TestEqual(TEXT("Description spawns at point without legacy height"), Desc->GetActorLocation(), Anchor);
	}
	Interactable->DismissActorBasedWidgets_Implementation();
	TestFalse(TEXT("Hidden UI leaves tick disabled"), Interactable->IsComponentTickEnabled());

	Interactable->InteractionTargetPointReference.PathToComponent = TEXT("MissingPoint");
	Interactable->RefreshInteractionTargetPoint();
	TestEqual(TEXT("Missing point preserves legacy height"),
		Interactable->GetInteractionWidgetLocation(120), LegacyLocation);
	Interactable->InteractionTargetPointReference = FComponentReference();
	Interactable->InteractionTargetPointReference.OverrideComponent = ForeignPoint;
	Interactable->RefreshInteractionTargetPoint();
	TestEqual(TEXT("Other actor component is rejected"),
		Interactable->GetInteractionWidgetLocation(120), LegacyLocation);
	Interactable->InteractionTargetPointReference.OverrideComponent = Point;
	Interactable->RefreshInteractionTargetPoint();
	Point->DestroyComponent();
	TestEqual(TEXT("Destroyed point falls back without another lookup"),
		Interactable->GetInteractionWidgetLocation(120), LegacyLocation);
	Interactable->InteractionTargetPointReference = FComponentReference();
	Interactable->RefreshInteractionTargetPoint();
	Interactable->InteractInfoWidgetComponent->SetVisibility(true);
	Interactable->UpdateWidgetPresentation();
	TestEqual(TEXT("Legacy UI retains both height and widget offset"),
		Interactable->InteractInfoWidgetComponent->GetComponentLocation(), LegacyLocation + FVector(1, 2, 3));
	return true;
}

#endif
