#if WITH_DEV_AUTOMATION_TESTS

#include "UI/UpgradeNodeWidget.h"
#include "UI/UpgradeDescWidget.h"

#include "Components/Button.h"
#include "Components/Image.h"
#include "Drone/Partner/PartnerCharacter.h"
#include "Engine/Engine.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "FirstPerson/FirstPersonPlayerController.h"
#include "Misc/AutomationTest.h"
#include "OutlierPlayerState.h"
#include "Upgrade/OutlierUpgradeComponent.h"
#include "Upgrade/OutlierUpgradeSetData.h"

// 테스트는 입력 장치를 흉내 내지 않고 실제 위젯의 상태 전이와 전송 경로를 호출한다.
struct FUpgradeNodeInteractionTestAccess
{
	static UUpgradeNodeWidget* MakeNode()
	{
		UDataTable* Table = NewObject<UDataTable>();
		Table->RowStruct = FOutlierUpgradeNodeRow::StaticStruct();
		Table->AddRow(TEXT("Node"), FOutlierUpgradeNodeRow());
		UOutlierUpgradeComponent* Component = NewObject<UOutlierUpgradeComponent>();
		Component->SetUpgradeDataTable(Table);
		UUpgradeNodeWidget* Node = NewObject<UUpgradeNodeWidget>();
		Node->SetUpgradeComponent(Component);
		FOutlierUpgradeNodeViewData View;
		Component->GetNodeViewData(TEXT("Node"), View);
		Node->InjectViewData(View);
		return Node;
	}

	static void Begin(UUpgradeNodeWidget* Node) { Node->bHoldingUpgrade = true; Node->HoldProgress = 0.0f; }
	static bool Advance(UUpgradeNodeWidget* Node, float Seconds) { return Node->AdvanceHold(Seconds); }
	static void Release(UUpgradeNodeWidget* Node) { Node->HandleReleased(); }
	static void Leave(UUpgradeNodeWidget* Node) { Node->HandleUnhovered(); }
	static void Deactivate(UUpgradeNodeWidget* Node) { Node->HandleApplicationActivationChanged(false); }
	static void Destruct(UUpgradeNodeWidget* Node) { Node->NativeDestruct(); }
	static bool CanBegin(UUpgradeNodeWidget* Node) { return Node->CanBeginHold(); }
	static void ReplaceContext(UUpgradeNodeWidget* Node)
	{
		Node->CachedUpgradeComponent->SetUpgradeSetData(NewObject<UOutlierUpgradeSetData>());
	}

	static void Pending(UUpgradeNodeWidget* Node, AActor* Owner, uint32 Id)
	{
		Node->ResetUpgradeInteraction();
		Node->PendingRequestId = Id;
		Node->PendingNodeRowName = TEXT("Node");
		Node->PendingUpgradeOwner = Owner;
		Node->HoldProgress = 1.0f;
		Node->RequestStartedTime = 100.0;
	}

	static void Result(UUpgradeNodeWidget* Node, AActor* Owner, uint32 Id, bool bSucceeded, FName Row = TEXT("Node"))
	{
		Node->HandleUpgradeResult(Id, Owner, Row, bSucceeded);
	}
	static void Timeout(UUpgradeNodeWidget* Node, double Now) { Node->CheckRequestTimeout(Now); }
	static void SetController(UUpgradeNodeWidget* Node, AFirstPersonPlayerController* Controller) { Node->UpgradeController = Controller; }
	static void Hide(UUpgradeNodeWidget* Node)
	{
		Node->CacheInteractionVisibility();
		Node->SetVisibility(ESlateVisibility::Collapsed);
		Node->TickUpgradeInteraction(0.0f);
	}

	static void SetPlayerState(AFirstPersonPlayerController* Controller, AOutlierPlayerState* State)
	{
		Controller->PlayerState = State;
	}
	static bool Purchase(AFirstPersonPlayerController* Controller, AActor* Owner, FName Row, UOutlierUpgradeSetData* Data)
	{
		return Controller->TryActivateUpgradeNodeForRequest(Owner, Row, Data);
	}
	static void DeliverResult(AFirstPersonPlayerController* Controller, AActor* Owner, uint32 Id, bool bSucceeded)
	{
		Controller->ClientUpgradePurchaseResult_Implementation(Id, Owner, TEXT("Node"), bSucceeded);
	}
	static void Submit(UUpgradeNodeWidget* Node, AFirstPersonPlayerController* Controller)
	{
		Node->UpgradeController = Controller;
		Node->HoldProgress = 1.0f;
		Node->RequestUpgrade();
	}

	static void SetStatusTextures(UUpgradeDescWidget* Desc, UImage* Image, const TArray<UTexture2D*>& Textures)
	{
		Desc->StatusImage = Image;
		Desc->AvailableTexture = Textures[0];
		Desc->InsufficientDisabledTexture = Textures[1];
		Desc->PrerequisiteDisabledTexture = Textures[2];
		Desc->PurchasedDisabledTexture = Textures[3];
	}
};

namespace UpgradeNodeInteractionTests
{
	struct FTestWorld
	{
		UWorld* World = nullptr;

		FTestWorld()
		{
			const FName Name = MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass(), TEXT("UpgradeHoldTestWorld"));
			World = UWorld::CreateWorld(EWorldType::Game, false, Name, GetTransientPackage());
			if (World && GEngine)
			{
				World->AddToRoot();
				GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
			}
		}

		~FTestWorld()
		{
			if (World)
			{
				World->DestroyWorld(false);
				if (GEngine) GEngine->DestroyWorldContext(World);
				World->RemoveFromRoot();
			}
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUpgradeHoldTimingTest, "Outlier.UI.Upgrade.Hold.Timing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUpgradeHoldTimingTest::RunTest(const FString& Parameters)
{
	using Access = FUpgradeNodeInteractionTestAccess;
	UUpgradeNodeWidget* Node = Access::MakeNode();
	Access::Begin(Node);
	TestFalse(TEXT("A short press cannot complete"), Access::Advance(Node, 0.25f));
	TestEqual(TEXT("Progress follows the hold duration"), Node->GetHoldProgress(), 0.25f);
	TestFalse(TEXT("Three quarters remains incomplete"), Access::Advance(Node, 0.5f));
	TestTrue(TEXT("Exactly one second completes"), Access::Advance(Node, 0.25f));
	TestEqual(TEXT("Completion reaches one"), Node->GetHoldProgress(), 1.0f);
	TestFalse(TEXT("Continued input cannot complete twice"), Access::Advance(Node, 2.0f));
	TestFalse(TEXT("Holding ends at completion"), Node->IsHoldingUpgrade());
	Node->ResetUpgradeInteraction();
	Node->HoldDuration = 2.0f;
	Access::Begin(Node);
	TestFalse(TEXT("A custom duration is respected"), Access::Advance(Node, 1.0f));
	TestEqual(TEXT("One second of a two second hold is half"), Node->GetHoldProgress(), 0.5f);
	Node->ResetUpgradeInteraction();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUpgradeHoldCancellationTest, "Outlier.UI.Upgrade.Hold.Cancellation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUpgradeHoldCancellationTest::RunTest(const FString& Parameters)
{
	using Access = FUpgradeNodeInteractionTestAccess;
	UUpgradeNodeWidget* Node = Access::MakeNode();
	Access::Begin(Node);
	Access::Advance(Node, 0.5f);
	Access::Release(Node);
	TestEqual(TEXT("Release clears progress immediately"), Node->GetHoldProgress(), 0.0f);
	TestFalse(TEXT("Release prevents a later completion"), Access::Advance(Node, 1.0f));
	Access::Begin(Node);
	Access::Advance(Node, 0.5f);
	Access::Leave(Node);
	TestEqual(TEXT("Leaving clears progress immediately"), Node->GetHoldProgress(), 0.0f);
	TestFalse(TEXT("Reentry alone cannot resume"), Access::Advance(Node, 1.0f));
	Access::Begin(Node);
	Access::Advance(Node, 0.5f);
	Access::Deactivate(Node);
	TestEqual(TEXT("App deactivation clears progress"), Node->GetHoldProgress(), 0.0f);
	Access::Begin(Node);
	Access::Advance(Node, 0.5f);
	Access::Hide(Node);
	TestEqual(TEXT("Hidden widgets cannot finish holding"), Node->GetHoldProgress(), 0.0f);
	Node->SetVisibility(ESlateVisibility::Visible);
	Access::Begin(Node);
	Access::Advance(Node, 0.5f);
	Access::Destruct(Node);
	TestFalse(TEXT("Destruction stops holding"), Node->IsHoldingUpgrade());
	TestEqual(TEXT("Reconstruction cannot inherit progress"), Node->GetHoldProgress(), 0.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUpgradeHoldEligibilityTest, "Outlier.UI.Upgrade.Hold.Eligibility",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUpgradeHoldEligibilityTest::RunTest(const FString& Parameters)
{
	using Access = FUpgradeNodeInteractionTestAccess;
	UUpgradeNodeWidget* Node = Access::MakeNode();
	TestTrue(TEXT("An affordable unlocked node is eligible"), Access::CanBegin(Node));
	Access::Begin(Node);
	Access::Advance(Node, 0.5f);
	Node->SetNodeState(EOutlierUpgradeNodeState::Unlocked, false);
	TestFalse(TEXT("Losing affordability cancels holding"), Node->IsHoldingUpgrade());
	TestEqual(TEXT("Losing affordability clears progress"), Node->GetHoldProgress(), 0.0f);
	TestFalse(TEXT("An unaffordable node is ineligible"), Access::CanBegin(Node));
	Node->SetNodeState(EOutlierUpgradeNodeState::Locked, true);
	TestFalse(TEXT("A locked node is ineligible even with enough currency"), Access::CanBegin(Node));
	Node->SetNodeState(EOutlierUpgradeNodeState::Activated, true);
	TestFalse(TEXT("An activated node is ineligible"), Access::CanBegin(Node));
	Node->SetNodeState(EOutlierUpgradeNodeState::Unlocked, true);
	Node->bAlwaysShowUnlockedIcon = true;
	TestFalse(TEXT("A display-only node is ineligible"), Access::CanBegin(Node));
	Node->bAlwaysShowUnlockedIcon = false;
	Access::Begin(Node);
	Access::Advance(Node, 0.5f);
	Access::ReplaceContext(Node);
	TestEqual(TEXT("Replacing data on the same component clears progress"), Node->GetHoldProgress(), 0.0f);
	Node->SetNodeState(EOutlierUpgradeNodeState::Unlocked, true);
	Access::Begin(Node);
	Access::Advance(Node, 0.5f);
	Node->InjectNodeData(TEXT("Other"), FOutlierUpgradeNodeRow());
	TestEqual(TEXT("Changing node identity clears old progress"), Node->GetHoldProgress(), 0.0f);
	Node->SetUpgradeComponent(nullptr);
	TestFalse(TEXT("A missing component is ineligible"), Access::CanBegin(Node));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUpgradePurchaseResponseOrderTest, "Outlier.UI.Upgrade.Purchase.ResponseOrder",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUpgradePurchaseResponseOrderTest::RunTest(const FString& Parameters)
{
	using Access = FUpgradeNodeInteractionTestAccess;
	UpgradeNodeInteractionTests::FTestWorld Context;
	if (!TestNotNull(TEXT("Response test world exists"), Context.World)) return false;
	AActor* Owner = Context.World->SpawnActor<AActor>();
	AActor* OtherOwner = Context.World->SpawnActor<AActor>();
	if (!TestNotNull(TEXT("Request owner exists"), Owner) || !TestNotNull(TEXT("Other owner exists"), OtherOwner)) return false;
	UUpgradeNodeWidget* Node = Access::MakeNode();
	Access::Pending(Node, Owner, 1);
	Access::Result(Node, Owner, 2, false);
	Access::Result(Node, OtherOwner, 1, false);
	Access::Result(Node, Owner, 1, false, TEXT("Other"));
	TestTrue(TEXT("Unrelated responses leave the current request pending"), Node->IsUpgradeRequestPending());
	Access::Result(Node, Owner, 1, true);
	TestTrue(TEXT("Success response waits for replicated activation"), Node->IsUpgradeRequestPending());
	TestTrue(TEXT("Success response cannot predict activation"), Node->GetNodeState() == EOutlierUpgradeNodeState::Unlocked);
	Access::Release(Node);
	Access::Leave(Node);
	TestEqual(TEXT("Release after submission preserves pending progress"), Node->GetHoldProgress(), 1.0f);
	Node->SetNodeState(EOutlierUpgradeNodeState::Activated, true);
	TestFalse(TEXT("Replicated activation completes the request"), Node->IsUpgradeRequestPending());
	TestEqual(TEXT("Activated state no longer needs temporary fill"), Node->GetHoldProgress(), 0.0f);
	Access::Result(Node, Owner, 1, false);
	TestTrue(TEXT("Late rejection cannot undo activation"), Node->GetNodeState() == EOutlierUpgradeNodeState::Activated);

	Node->SetNodeState(EOutlierUpgradeNodeState::Unlocked, true);
	Access::Pending(Node, Owner, 3);
	Access::Result(Node, Owner, 3, false);
	TestFalse(TEXT("Matching rejection clears pending state"), Node->IsUpgradeRequestPending());
	TestEqual(TEXT("Rejection clears progress"), Node->GetHoldProgress(), 0.0f);
	TestTrue(TEXT("Rejection permits a new hold"), Access::CanBegin(Node));
	Access::Pending(Node, Owner, 4);
	Node->SetNodeState(EOutlierUpgradeNodeState::Activated, true);
	Access::Result(Node, Owner, 4, true);
	TestFalse(TEXT("Replication before response also completes once"), Node->IsUpgradeRequestPending());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUpgradePurchaseTimeoutTest, "Outlier.UI.Upgrade.Purchase.Timeout",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUpgradePurchaseTimeoutTest::RunTest(const FString& Parameters)
{
	using Access = FUpgradeNodeInteractionTestAccess;
	UpgradeNodeInteractionTests::FTestWorld Context;
	if (!TestNotNull(TEXT("Timeout test world exists"), Context.World)) return false;
	AActor* Owner = Context.World->SpawnActor<AActor>();
	if (!TestNotNull(TEXT("Request owner exists"), Owner)) return false;
	AFirstPersonPlayerController* Controller = Context.World->SpawnActor<AFirstPersonPlayerController>();
	if (!TestNotNull(TEXT("Request controller exists"), Controller)) return false;
	UUpgradeNodeWidget* Node = Access::MakeNode();
	Access::SetController(Node, Controller);
	Access::Pending(Node, Owner, 10);
	Access::Timeout(Node, 104.99);
	TestTrue(TEXT("Request remains pending before its deadline"), Node->IsUpgradeRequestPending());
	Access::Timeout(Node, 105.0);
	TestFalse(TEXT("Deadline clears local pending state"), Node->IsUpgradeRequestPending());
	Access::Pending(Node, Owner, 11);
	Access::Result(Node, Owner, 10, false);
	TestTrue(TEXT("An expired request cannot cancel a new request"), Node->IsUpgradeRequestPending());
	Node->ResetUpgradeInteraction();
	Access::Result(Node, Owner, 11, true);
	TestFalse(TEXT("Closing the screen invalidates the old request"), Node->IsUpgradeRequestPending());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUpgradePurchaseAuthorityTest, "Outlier.UI.Upgrade.Purchase.Authority",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUpgradePurchaseAuthorityTest::RunTest(const FString& Parameters)
{
	using Access = FUpgradeNodeInteractionTestAccess;
	UpgradeNodeInteractionTests::FTestWorld Context;
	if (!TestNotNull(TEXT("Purchase test world exists"), Context.World)) return false;
	// Actor RPC는 ProcessEvent를 거친다. 월드의 Actor 초기화가 빠지면 서버/응답 함수가 실행되지 않는다.
	// BeginPlay 없이 RPC 실행 조건만 준비해, 실제 위젯 요청부터 서버 비용 차감과 응답까지 검증한다.
	Context.World->InitializeActorsForPlay(FURL());
	if (!TestTrue(TEXT("The test world can dispatch actor RPCs"), Context.World->AreActorsInitialized())) return false;
	FActorSpawnParameters SpawnParameters;
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AFirstPersonPlayerController* Controller = Context.World->SpawnActor<AFirstPersonPlayerController>();
	AOutlierPlayerState* State = Context.World->SpawnActor<AOutlierPlayerState>();
	APartnerCharacter* Partner = Context.World->SpawnActor<APartnerCharacter>(
		FVector::ZeroVector, FRotator::ZeroRotator, SpawnParameters);
	if (!TestNotNull(TEXT("Controller exists"), Controller) || !TestNotNull(TEXT("PlayerState exists"), State)
		|| !TestNotNull(TEXT("Partner exists"), Partner)) return false;
	Access::SetPlayerState(Controller, State);
	State->SetPairId(1);
	State->SetPlayerRole(EOutlierPlayerRole::Partner);
	State->SetPartnerCharacter(Partner);
	Partner->SetPlayerState(State);
	TestTrue(TEXT("Test currency is granted"), State->AddNode(3));

	UDataTable* Table = NewObject<UDataTable>();
	Table->RowStruct = FOutlierUpgradeNodeRow::StaticStruct();
	FOutlierUpgradeNodeRow Row;
	Row.Role = EOutlierUpgradeRole::Partner;
	Row.Cost = 2;
	Table->AddRow(TEXT("Node"), Row);
	Row.Cost = 5;
	Table->AddRow(TEXT("Expensive"), Row);
	Row.Cost = 0;
	Row.ParentId = TEXT("Expensive");
	Table->AddRow(TEXT("Locked"), Row);
	UOutlierUpgradeSetData* Data = NewObject<UOutlierUpgradeSetData>();
	Data->UpgradeRole = EOutlierUpgradeRole::Partner;
	Data->UpgradeDataTable = Table;
	UOutlierUpgradeComponent* Component = Partner->GetUpgradeComponent();
	if (!TestNotNull(TEXT("Partner upgrade component exists"), Component)) return false;
	Component->SetUpgradeSetData(Data);

	UOutlierUpgradeSetData* OtherData = NewObject<UOutlierUpgradeSetData>();
	OtherData->UpgradeRole = EOutlierUpgradeRole::Partner;
	OtherData->UpgradeDataTable = Table;
	TestFalse(TEXT("A different data asset cannot replace the server context"), Access::Purchase(Controller, Partner, TEXT("Node"), OtherData));
	OtherData->UpgradeRole = EOutlierUpgradeRole::Shooter;
	TestFalse(TEXT("A different role is rejected"), Access::Purchase(Controller, Partner, TEXT("Node"), OtherData));
	AActor* OtherOwner = Context.World->SpawnActor<AActor>();
	TestFalse(TEXT("A different owner is rejected"), Access::Purchase(Controller, OtherOwner, TEXT("Node"), Data));
	TestFalse(TEXT("An unknown row is rejected"), Access::Purchase(Controller, Partner, TEXT("Missing"), Data));
	// [Upgrade]의 대괄호가 정규식으로 해석되지 않도록 실제 로그 문자열 그대로 비교한다.
	AddExpectedErrorPlain(TEXT("[Upgrade] Activate failed: insufficient node"), EAutomationExpectedErrorFlags::Contains, 1);
	TestFalse(TEXT("The server rejects insufficient currency"), Access::Purchase(Controller, Partner, TEXT("Expensive"), Data));
	AddExpectedErrorPlain(TEXT("[Upgrade] Activate failed: locked"), EAutomationExpectedErrorFlags::Contains, 1);
	TestFalse(TEXT("The server rejects a missing prerequisite"), Access::Purchase(Controller, Partner, TEXT("Locked"), Data));
	TestEqual(TEXT("Rejected requests do not spend currency"), State->GetNodeCount(), 3);

	UUpgradeNodeWidget* Node = NewObject<UUpgradeNodeWidget>();
	Node->NodeRowName = TEXT("Node");
	Node->SetUpgradePlayerState(State);
	Node->SetUpgradeComponent(Component);
	Access::Begin(Node);
	TestFalse(TEXT("Half a hold does not finish"), Access::Advance(Node, 0.5f));
	TestEqual(TEXT("An incomplete hold does not spend currency"), State->GetNodeCount(), 3);
	TestFalse(TEXT("An incomplete hold does not activate"), Component->IsNodeActivated(TEXT("Node")));
	TestTrue(TEXT("The full hold finishes once"), Access::Advance(Node, 0.5f));

	int32 ResponseCount = 0;
	bool bResponseSucceeded = false;
	uint32 ResponseId = 0;
	const FDelegateHandle ResponseHandle = Controller->OnUpgradePurchaseResult.AddLambda(
		[&](uint32 Id, AActor* Owner, FName Name, bool bSucceeded)
		{
			++ResponseCount;
			ResponseId = Id;
			bResponseSucceeded = bSucceeded;
		});
	// 실제 위젯 전송 경로를 사용해 Listen 호스트처럼 상태 변경이 응답보다 먼저 일어나는 경우를 검증한다.
	Access::Submit(Node, Controller);
	TestEqual(TEXT("One completed hold receives one response"), ResponseCount, 1);
	TestTrue(TEXT("The response has a request identity"), ResponseId != 0);
	TestTrue(TEXT("The server accepts the valid purchase"), bResponseSucceeded);
	TestEqual(TEXT("The server spends the cost once"), State->GetNodeCount(), 1);
	TestTrue(TEXT("Authoritative activation reaches the widget"), Node->GetNodeState() == EOutlierUpgradeNodeState::Activated);
	TestFalse(TEXT("Synchronous activation does not leave a pending request"), Node->IsUpgradeRequestPending());
	TestFalse(TEXT("The held input cannot complete again"), Access::Advance(Node, 1.0f));
	AddExpectedErrorPlain(TEXT("[Upgrade] Activate failed: already activated"), EAutomationExpectedErrorFlags::Contains, 1);
	TestFalse(TEXT("The server also rejects a duplicate purchase"), Access::Purchase(Controller, Partner, TEXT("Node"), Data));
	TestEqual(TEXT("A duplicate request cannot spend the cost twice"), State->GetNodeCount(), 1);
	Access::DeliverResult(Controller, Partner, ResponseId, false);
	TestTrue(TEXT("A late response cannot undo activation"), Node->GetNodeState() == EOutlierUpgradeNodeState::Activated);
	Controller->OnUpgradePurchaseResult.Remove(ResponseHandle);
	Node->ResetUpgradeInteraction();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUpgradeDescStatusImageTest, "Outlier.UI.Upgrade.Description.StatusImage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUpgradeDescStatusImageTest::RunTest(const FString& Parameters)
{
	// 추상 설명창의 실제 BP 클래스를 사용하되, 이미지 선택은 임시 텍스처로 독립적으로 검증한다.
	UClass* DescClass = LoadClass<UUpgradeDescWidget>(nullptr,
		TEXT("/Game/Blueprints/Widget/FirstPerson/Upgrade/WBP_UpgradeRegularDesc.WBP_UpgradeRegularDesc_C"));
	if (!TestNotNull(TEXT("Regular description BP exists"), DescClass)) return false;
	UUpgradeDescWidget* Desc = NewObject<UUpgradeDescWidget>(GetTransientPackage(), DescClass);
	UImage* Image = NewObject<UImage>(Desc);
	TArray<UTexture2D*> Textures;
	for (int32 Index = 0; Index < 4; ++Index) Textures.Add(NewObject<UTexture2D>());
	FUpgradeNodeInteractionTestAccess::SetStatusTextures(Desc, Image, Textures);
	const FOutlierUpgradeNodeRow Row;
	Desc->UpdateNodeData(TEXT("Node"), Row, EOutlierUpgradeNodeState::Unlocked, 1, true);
	TestTrue(TEXT("Purchasable nodes use the new default image"), Image->GetBrush().GetResourceObject() == Textures[0]);
	Desc->UpdateNodeData(TEXT("Node"), Row, EOutlierUpgradeNodeState::Unlocked, 0, false);
	TestTrue(TEXT("Insufficient currency uses its existing image"), Image->GetBrush().GetResourceObject() == Textures[1]);
	Desc->UpdateNodeData(TEXT("Node"), Row, EOutlierUpgradeNodeState::Locked, 1, true);
	TestTrue(TEXT("Locked nodes use the prerequisite image"), Image->GetBrush().GetResourceObject() == Textures[2]);
	Desc->UpdateNodeData(TEXT("Node"), Row, EOutlierUpgradeNodeState::Activated, 0, false);
	TestTrue(TEXT("Activation takes priority over affordability"), Image->GetBrush().GetResourceObject() == Textures[3]);
	TestTrue(TEXT("Status images cannot intercept node input"), Image->GetVisibility() == ESlateVisibility::HitTestInvisible);
	return true;
}

#endif
