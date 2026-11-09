#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Components/SceneComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Drone/Partner/HackableComponent.h"
#include "Drone/Partner/HackGameplayTags.h"
#include "Drone/Partner/HackTestActor.h"
#include "Enemy/AutoTurret.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Interaction/InteractableSwitch.h"
#include "Interaction/InteractionStatMachine.h"
#include "Jumper/Jumper.h"
#include "Drone/Partner/MagneticGeneratorTower.h"
#include "Interface/HackableInterface.h"
#include "Misc/AutomationTest.h"
#include "Misc/DataValidation.h"
#include "Misc/ScopeExit.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHackTargetPointTest,
	"Outlier.Hack.TargetPoint.ReferenceAndFallback",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHackTargetPointTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false,
		MakeUniqueObjectName(nullptr, UWorld::StaticClass(), TEXT("HackTargetPointTest")));
	if (!TestNotNull(TEXT("Test world exists"), World)) return false;
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());
	ON_SCOPE_EXIT
	{
		World->DestroyWorld(false);
		GEngine->DestroyWorldContext(World);
		World->RemoveFromRoot();
	};

	// BP가 루트를 제공하는 테스트 Actor에도 기존 루트를 교체하지 않고 목표점을 지정한다.
	const auto AddSceneComponent = [](AActor* Owner, FName Name, USceneComponent* Parent)
	{
		USceneComponent* Component = NewObject<USceneComponent>(Owner, Name);
		Owner->AddInstanceComponent(Component);
		if (Parent) Component->SetupAttachment(Parent);
		else Owner->SetRootComponent(Component);
		Component->RegisterComponent();
		return Component;
	};
	AHackTestActor* Actor = World->SpawnActor<AHackTestActor>();
	AHackTestActor* OtherActor = World->SpawnActor<AHackTestActor>();
	AAutoTurret* Turret = World->SpawnActor<AAutoTurret>();
	if (!TestNotNull(TEXT("Hack actor exists"), Actor)
		|| !TestNotNull(TEXT("Other actor exists"), OtherActor)
		|| !TestNotNull(TEXT("Turret exists"), Turret)) return false;
	USceneComponent* Root = AddSceneComponent(Actor, TEXT("TestRoot"), nullptr);
	USceneComponent* Target = AddSceneComponent(Actor, TEXT("TestTargetPoint"), Root);
	USceneComponent* OtherRoot = AddSceneComponent(OtherActor, TEXT("OtherRoot"), nullptr);
	UHackableComponent* Hackable = Actor->GetHackableComponent();
	FVector Location;
	constexpr double PositionTolerance = 0.01; // UE 좌표 기준 0.01cm 허용오차.

	Actor->SetActorLocation(FVector(100, 200, 300));
	Hackable->RefreshHackTargetPoint();
	TestFalse(TEXT("Empty reference does not implicitly select root"), Hackable->TryGetHackTargetLocation(Location));
	TestTrue(TEXT("Empty reference preserves actor origin"),
		IHackableInterface::ResolveHackTargetLocation(Actor).Equals(Actor->GetActorLocation(), PositionTolerance));
	// 터렛도 이제 기본 목표점이 있으므로, 호환 경로 검사는 참조를 명시적으로 비운다.
	Turret->GetHackableComponent()->HackTargetPointReference = FComponentReference();
	Turret->GetHackableComponent()->RefreshHackTargetPoint();
	TestTrue(TEXT("Empty turret reference preserves combat view fallback"),
		Turret->GetHackTargetLocation().Equals(Turret->GetPawnViewLocation(), PositionTolerance));
	TestTrue(TEXT("Null actor query remains safe"),
		IHackableInterface::ResolveHackTargetLocation(nullptr).IsZero());

	Hackable->HackTargetPointReference.PathToComponent = Target->GetName();
	// 최초 등록 이후 생성된 BP 목표점도 플레이 시작 시점에는 참조에 반영되어야 한다.
	Actor->DispatchBeginPlay();
	TestTrue(TEXT("BeginPlay resolves target created after initial registration"), Hackable->TryGetHackTargetLocation(Location));
	// 재등록 시 참조가 다시 해결되는지 확인한다. 조회 함수에 별도 검색을 요구하지 않는다.
	Hackable->UnregisterComponent();
	TestFalse(TEXT("Unregister clears cached target"), Hackable->TryGetHackTargetLocation(Location));
	Hackable->RegisterComponent();
	Target->SetRelativeLocation(FVector(40, 10, 75));
	Actor->SetActorTransform(FTransform(FRotator(10, 35, 5), FVector(250, 400, 100), FVector(2, 3, 1.5)));
	TestTrue(TEXT("Same-owner path resolves after registration"), Hackable->TryGetHackTargetLocation(Location));
	TestTrue(TEXT("Interface uses component world transform"),
		IHackableInterface::ResolveHackTargetLocation(Actor).Equals(Target->GetComponentLocation(), PositionTolerance));
	Target->SetRelativeLocation(FVector(-50, 30, 125));
	TestTrue(TEXT("Moving target updates location without refreshing reference"),
		IHackableInterface::ResolveHackTargetLocation(Actor).Equals(Target->GetComponentLocation(), PositionTolerance));
	TestFalse(TEXT("Configuring a point does not grant hack target tags"), Hackable->IsHackTargetType());
	FDataValidationContext ValidContext;
	TestTrue(TEXT("Valid target passes data validation"), Hackable->IsDataValid(ValidContext) != EDataValidationResult::Invalid);
	TestEqual(TEXT("Valid target has no validation errors"), ValidContext.GetNumErrors(), uint32(0));

	// 지정된 컴포넌트가 사라지면 캐시를 검색으로 복구하지 않고 Actor별 대체 계산을 사용한다.
	Target->DestroyComponent();
	TestFalse(TEXT("Destroyed cached target is unavailable"), Hackable->TryGetHackTargetLocation(Location));
	TestTrue(TEXT("Destroyed target falls back to actor origin"),
		IHackableInterface::ResolveHackTargetLocation(Actor).Equals(Actor->GetActorLocation(), PositionTolerance));
	FDataValidationContext MissingContext;
	TestEqual(TEXT("Missing configured target is diagnosed"), Hackable->IsDataValid(MissingContext), EDataValidationResult::Invalid);

	Hackable->HackTargetPointReference = FComponentReference();
	Hackable->HackTargetPointReference.OverrideComponent = OtherRoot;
	Hackable->RefreshHackTargetPoint();
	TestFalse(TEXT("Foreign component override is rejected"), Hackable->TryGetHackTargetLocation(Location));
	FDataValidationContext ForeignContext;
	TestEqual(TEXT("Foreign target is diagnosed"), Hackable->IsDataValid(ForeignContext), EDataValidationResult::Invalid);

	Hackable->HackTargetPointReference = FComponentReference();
	Hackable->HackTargetPointReference.OtherActor = OtherActor;
	Hackable->HackTargetPointReference.PathToComponent = OtherRoot->GetName();
	Hackable->RefreshHackTargetPoint();
	TestFalse(TEXT("Foreign actor path is rejected"), Hackable->TryGetHackTargetLocation(Location));
	Hackable->HackTargetPointReference = FComponentReference();
	Hackable->HackTargetPointReference.OtherActor = Actor;
	Hackable->RefreshHackTargetPoint();
	TestFalse(TEXT("Actor-only selection does not implicitly select root"), Hackable->TryGetHackTargetLocation(Location));

	Hackable->HackTargetPointReference = FComponentReference();
	Hackable->HackTargetPointReference.OverrideComponent = Hackable;
	Hackable->RefreshHackTargetPoint();
	TestFalse(TEXT("Non-scene component is rejected"), Hackable->TryGetHackTargetLocation(Location));
	USceneComponent* EditorPoint = AddSceneComponent(Actor, TEXT("EditorPoint"), Root);
	EditorPoint->bIsEditorOnly = true;
	Hackable->HackTargetPointReference.OverrideComponent = EditorPoint;
	Hackable->RefreshHackTargetPoint();
	TestFalse(TEXT("Editor-only target is rejected"), Hackable->TryGetHackTargetLocation(Location));
	FDataValidationContext EditorOnlyContext;
	TestEqual(TEXT("Editor-only target is diagnosed"), Hackable->IsDataValid(EditorOnlyContext), EDataValidationResult::Invalid);

	USceneComponent* Replacement = AddSceneComponent(Actor, TEXT("ReplacementPoint"), Root);
	Hackable->HackTargetPointReference.OverrideComponent = Replacement;
	Hackable->RefreshHackTargetPoint();
	TestTrue(TEXT("Explicit override resolves"), Hackable->TryGetHackTargetLocation(Location));
	Replacement->DestroyComponent();
	Hackable->RefreshHackTargetPoint();
	TestFalse(TEXT("Expired override never selects root"), Hackable->TryGetHackTargetLocation(Location));

	Hackable->HackTargetPointReference = FComponentReference();
	Hackable->HackTargetPointReference.PathToComponent = Root->GetName();
	Hackable->RefreshHackTargetPoint();
	TestTrue(TEXT("Explicit root selection is supported"), Hackable->TryGetHackTargetLocation(Location));
	Hackable->HackTargetPointReference = FComponentReference();
	Hackable->HackTags.AddTag(HackGameplayTags::Target::NonPossessable());
	Hackable->RefreshHackTargetPoint();
	FDataValidationContext UnconfiguredContext;
	TestTrue(TEXT("Legacy unconfigured target is not invalidated"),
		Hackable->IsDataValid(UnconfiguredContext) != EDataValidationResult::Invalid);
	TestEqual(TEXT("Legacy hack target gets migration warning"), UnconfiguredContext.GetNumWarnings(), uint32(1));

	USceneComponent* TurretPoint = AddSceneComponent(Turret, TEXT("TurretTestPoint"), Turret->GetRootComponent());
	TurretPoint->SetRelativeLocation(FVector(15, 20, 180));
	const FVector CombatViewBefore = Turret->GetPawnViewLocation();
	Turret->GetHackableComponent()->HackTargetPointReference.PathToComponent = TurretPoint->GetName();
	Turret->GetHackableComponent()->RefreshHackTargetPoint();
	TestTrue(TEXT("Turret interface prefers explicit point"),
		IHackableInterface::ResolveHackTargetLocation(Turret).Equals(TurretPoint->GetComponentLocation(), PositionTolerance));
	TestTrue(TEXT("Hack reference leaves combat view unchanged"),
		Turret->GetPawnViewLocation().Equals(CombatViewBefore, PositionTolerance));
	TurretPoint->DestroyComponent();
	TestTrue(TEXT("Invalid turret target returns to combat view fallback"),
		Turret->GetHackTargetLocation().Equals(Turret->GetPawnViewLocation(), PositionTolerance));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHackTargetPointDefaultsTest,
	"Outlier.Hack.TargetPoint.NativeDefaults",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHackTargetPointDefaultsTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false,
		MakeUniqueObjectName(nullptr, UWorld::StaticClass(), TEXT("HackTargetPointDefaultsTest")));
	if (!TestNotNull(TEXT("Defaults test world exists"), World)) return false;
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());
	ON_SCOPE_EXIT
	{
		World->DestroyWorld(false);
		GEngine->DestroyWorldContext(World);
		World->RemoveFromRoot();
	};

	// 스위치 기반 클래스는 추상이므로 실제 일반 스위치에서 상속 결과를 확인한다.
	UClass* TargetClasses[] = {
		AEnemyBase::StaticClass(), AInteractableSwitch::StaticClass(), AInteractionStatMachine::StaticClass(),
		AJumper::StaticClass(), AMagneticGeneratorTower::StaticClass(), AAutoTurret::StaticClass()
	};
	constexpr double PositionTolerance = 0.01; // 월드 위치 비교 허용오차: 0.01cm.
	for (UClass* TargetClass : TargetClasses)
	{
		AddInfo(FString::Printf(TEXT("Checking native target defaults: %s"), *TargetClass->GetName()));
		AActor* Actor = World->SpawnActor<AActor>(TargetClass);
		if (!TestNotNull(TEXT("Native actor exists"), Actor)) return false;
		IHackableInterface* HackInterface = Cast<IHackableInterface>(Actor);
		if (!TestNotNull(TEXT("Actor implements hacking interface"), HackInterface)) return false;
		UHackableComponent* Hackable = HackInterface->GetHackableComponent();
		if (!TestNotNull(TEXT("Existing hackable component retained"), Hackable)) return false;
		TestEqual(TEXT("Default reference uses reflected property name"),
			Hackable->HackTargetPointReference.ComponentProperty, FName(TEXT("HackTargetPoint")));
		USceneComponent* Target = Cast<USceneComponent>(Hackable->HackTargetPointReference.GetComponent(Actor));
		if (!TestNotNull(TEXT("Default reference resolves on its own actor"), Target)) return false;
		TestTrue(TEXT("Target belongs to actor"), Target->GetOwner() == Actor);
		TestTrue(TEXT("Inherited target transform is editable"), Target->bEditableWhenInherited);
		TestFalse(TEXT("Target is available outside editor"), Target->IsEditorOnly());
		TestTrue(TEXT("Target loads on server"), Target->NeedsLoadForServer());
		TestTrue(TEXT("Default relative transform is identity"), Target->GetRelativeTransform().Equals(FTransform::Identity));
		TestFalse(TEXT("Target introduces no ticking"), Target->PrimaryComponentTick.bCanEverTick);
		TArray<USceneComponent*> SceneComponents;
		Actor->GetComponents(SceneComponents);
		int32 TargetCount = 0;
		for (const USceneComponent* Component : SceneComponents)
		{
			if (Component->GetFName() == FName(TEXT("HackTargetPoint"))) ++TargetCount;
		}
		TestEqual(TEXT("Native hierarchy has exactly one target point"), TargetCount, 1);

		if (AAutoTurret* Turret = Cast<AAutoTurret>(Actor))
		{
			TestTrue(TEXT("Turret target attaches to body mesh"), Target->GetAttachParent() == Turret->GetMesh());
			TestEqual(TEXT("Turret target uses deployment neck bone"), Target->GetAttachSocketName(), FName(TEXT("TurretNeck")));
		}
		else
		{
			TestTrue(TEXT("Other targets attach to existing root"), Target->GetAttachParent() == Actor->GetRootComponent());
			TestTrue(TEXT("Other default targets start at actor origin"),
				HackInterface->GetHackTargetLocation().Equals(Actor->GetActorLocation(), PositionTolerance));
		}
		if (Actor->IsA<AInteractableSwitch>())
		{
			TestFalse(TEXT("General switch stays outside hack target types"), Hackable->IsHackTargetType());
		}

		const FGameplayTagContainer OriginalTags = Hackable->HackTags;
		Target->SetRelativeLocation(FVector(23, 47, 89));
		Actor->SetActorTransform(FTransform(FRotator(10, 35, 5), FVector(250, 400, 100), FVector(2, 3, 1.5)));
		if (AAutoTurret* Turret = Cast<AAutoTurret>(Actor))
		{
			// 목표점과 조준 피벗은 몸체 아래의 별도 가지이므로 머리 조준에 목표점이 휘둘리지 않는다.
			const FTransform TargetBefore = Target->GetComponentTransform();
			Turret->GetWeaponMuzzleComponent(false)->AddLocalRotation(FRotator(15, 45, 0));
			TestTrue(TEXT("Turret head rotation leaves target transform unchanged"),
				Target->GetComponentTransform().Equals(TargetBefore, PositionTolerance));
		}
		TestTrue(TEXT("Default query follows target world transform"),
			HackInterface->GetHackTargetLocation().Equals(Target->GetComponentLocation(), PositionTolerance));

		// 사용자 지정 참조는 재등록 뒤에도 보존한다. 기본 목표점으로 강제 복구하지 않는다.
		USceneComponent* CustomPoint = NewObject<USceneComponent>(Actor, TEXT("CustomHackPoint"));
		Actor->AddInstanceComponent(CustomPoint);
		CustomPoint->SetupAttachment(Actor->GetRootComponent());
		CustomPoint->SetRelativeLocation(FVector(-100, 25, 50));
		CustomPoint->RegisterComponent();
		Hackable->HackTargetPointReference = FComponentReference();
		Hackable->HackTargetPointReference.PathToComponent = CustomPoint->GetName();
		Hackable->UnregisterComponent();
		Hackable->RegisterComponent();
		TestTrue(TEXT("Instance override survives registration"),
			HackInterface->GetHackTargetLocation().Equals(CustomPoint->GetComponentLocation(), PositionTolerance));
		TestTrue(TEXT("Target setup leaves gameplay tags unchanged"), Hackable->HackTags == OriginalTags);
	}
	return true;
}

#endif
