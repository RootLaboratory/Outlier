#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Interaction/InteractableComponent.h"

#include "Drone/Partner/PartnerEMPComponent.h"
#include "Drone/Partner/PartnerHackComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Tutorial/WorldGuide/OutlierWorldGuideTargetComponent.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FInteractableWorldlessEndPlayTest,
	"Outlier.Interaction.Lifecycle.WorldlessBeginDestroy",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FInteractableWorldlessEndPlayTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	if (!TestNotNull(TEXT("Test world exists"), World))
	{
		return false;
	}
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	ON_SCOPE_EXIT
	{
		World->DestroyWorld(false);
		GEngine->DestroyWorldContext(World);
	};
	const TArray<UClass*> ComponentClasses = {
		UInteractableComponent::StaticClass(), UPartnerHackComponent::StaticClass(),
		UPartnerEMPComponent::StaticClass(), UOutlierWorldGuideTargetComponent::StaticClass()
	};
	for (UClass* ComponentClass : ComponentClasses)
	{
		UActorComponent* Component = NewObject<UActorComponent>(GetTransientPackage(), ComponentClass);
		const FString Prefix = ComponentClass->GetName() + TEXT(": ");
		Component->RegisterComponentWithWorld(World);
		Component->BeginPlay();
		// 등록 해제로 월드 참조가 먼저 사라져도 BeginPlay 상태는 남는다.
		// 이후 BeginDestroy가 실제 엔진 경로를 통해 각 컴포넌트의 EndPlay를 호출한다.
		Component->UnregisterComponent();
		TestNull(Prefix + TEXT("Late component cleanup has no world"), Component->GetWorld());
		TestTrue(Prefix + TEXT("Component still requires EndPlay"), Component->HasBegunPlay());
		Component->ConditionalBeginDestroy();
		TestFalse(Prefix + TEXT("Worldless BeginDestroy completes EndPlay"), Component->HasBegunPlay());
		TestTrue(Prefix + TEXT("Component destruction continues"), Component->HasAnyFlags(RF_BeginDestroyed));
	}
	return true;
}

#endif
