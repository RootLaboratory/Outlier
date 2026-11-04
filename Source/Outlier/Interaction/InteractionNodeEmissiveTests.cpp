#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Interaction/InteractionNode.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameplayTags/OutlierGameplayTags.h"
#include "FirstPerson/FirstPersonCharacter.h"
#include "Interaction/InteractableComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include <limits>

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FInteractionNodeProgressTest, "Outlier.Interaction.Node.Progress",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FInteractionNodeProgressTest::RunTest(const FString& Parameters)
{
	FInteractionNodeHoldState State;
	State.bHolding = true;
	State.ServerStartTime = 10.0;
	State.Duration = 2.0f;
	TestEqual(TEXT("Before approval time progress is clamped to zero"), State.GetProgress(9.0), 0.0f);
	TestEqual(TEXT("Start is zero"), State.GetProgress(10.0), 0.0f);
	TestEqual(TEXT("Midpoint is shared by UI and emissive"), State.GetProgress(11.0), 0.5f);
	TestEqual(TEXT("Completion is one"), State.GetProgress(12.0), 1.0f);
	TestEqual(TEXT("Awaiting result remains one"), State.GetProgress(20.0), 1.0f);
	State.bHolding = false;
	TestEqual(TEXT("Cancelled state resets progress"), State.GetProgress(11.0), 0.0f);
	State.bHolding = true;
	State.ServerStartTime = 20.0;
	TestEqual(TEXT("Retry uses a new start time"), State.GetProgress(20.0), 0.0f);
	for (float Duration : {0.0f, -1.0f, std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity()})
	{
		State.Duration = Duration;
		TestEqual(TEXT("Invalid duration cannot advance progress"), State.GetProgress(21.0), 0.0f);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FInteractionNodePresentationTest, "Outlier.Interaction.Node.Presentation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FInteractionNodePresentationTest::RunTest(const FString& Parameters)
{
	// 월드가 없는 기본 객체는 시각 조회와 잘못된 시작 요청에도 진행 상태를 변경하지 않는다.
	AInteractionNode* Template = GetMutableDefault<AInteractionNode>();
	TestEqual(TEXT("Worldless template preserves its presentation time"),
		Template->GetPresentationTime(), Template->HoldState.ServerStartTime);
	const int32 TemplateRevision = Template->HoldState.Revision;
	Template->BeginHoldPresentation(nullptr, 1, 2.0f);
	TestEqual(TEXT("Template cannot publish a hold attempt"), Template->HoldState.Revision, TemplateRevision);

	if (!TestNotNull(TEXT("Engine exists"), GEngine)) return false;
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false,
		MakeUniqueObjectName(nullptr, UWorld::StaticClass(), TEXT("NodePresentationTest")));
	if (!TestNotNull(TEXT("World exists"), World)) return false;
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());
	ON_SCOPE_EXIT
	{
		World->DestroyWorld(false);
		GEngine->DestroyWorldContext(World);
		World->RemoveFromRoot();
	};

	AInteractionNode* First = World->SpawnActor<AInteractionNode>();
	AInteractionNode* Second = World->SpawnActor<AInteractionNode>();
	if (!TestNotNull(TEXT("First node exists"), First) || !TestNotNull(TEXT("Second node exists"), Second)) return false;
	TestTrue(TEXT("First component references its runtime owner"), First->GetInteractableComponent()->InteractionNode == First);
	TestTrue(TEXT("Second component references its runtime owner"), Second->GetInteractableComponent()->InteractionNode == Second);
	// 복제된 템플릿 참조가 남아 있어도 등록 과정에서 실제 소유자로 교체돼야 한다.
	First->GetInteractableComponent()->InteractionNode = Template;
	First->GetInteractableComponent()->ReregisterComponent();
	TestTrue(TEXT("Reregister repairs a template reference"), First->GetInteractableComponent()->InteractionNode == First);
	FGameplayTagContainer PartnerTags;
	PartnerTags.AddTag(OutlierGameplayTags::Actor::Role::Partner());
	FGameplayTagContainer ShooterTags;
	ShooterTags.AddTag(OutlierGameplayTags::Actor::Role::Shooter());
	TestTrue(TEXT("Partner can interact"), First->GetInteractableComponent()->CanInteract(PartnerTags));
	TestFalse(TEXT("Shooter cannot interact"), First->GetInteractableComponent()->CanInteract(ShooterTags));
	TestFalse(TEXT("Missing role cannot interact"), First->GetInteractableComponent()->CanInteract(FGameplayTagContainer()));

	First->HoldState.bHolding = true;
	First->HoldState.AttemptId = 7;
	First->HoldState.Duration = 2.0f;
	First->HoldState.ServerStartTime = World->GetTimeSeconds() - 1.0;
	TestEqual(TEXT("Current holding state is half complete"), First->GetHoldProgress(), 0.5f);
	First->CanceledAttemptId = 7;
	TestEqual(TEXT("Late holding snapshot cannot relight a cancelled attempt"), First->GetHoldProgress(), 0.0f);
	First->LocalAttemptId = 8;
	TestEqual(TEXT("Retry cannot reuse the previous snapshot"), First->GetHoldProgress(), 0.0f);
	First->HoldState.AttemptId = 8;
	First->HoldState.ServerStartTime = World->GetTimeSeconds();
	TestEqual(TEXT("Accepted retry begins at zero"), First->GetHoldProgress(), 0.0f);
	First->bCollected = true;
	First->HoldState.ServerStartTime -= 2.0;
	TestEqual(TEXT("Collected overrides a late completed holding snapshot"), First->GetHoldProgress(), 0.0f);
	TestFalse(TEXT("Collected nodes do not need presentation updates"), First->NeedsPresentationUpdate());

	AFirstPersonCharacter* Character = World->SpawnActor<AFirstPersonCharacter>();
	if (!TestNotNull(TEXT("Interaction requester exists"), Character)) return false;
	First->bCollected = false;
	Character->HoldingInteractActor = First;
	// Character는 번호를 보관하지 않는다. 노드가 발급한 번호로 기존 응답 순서 검증을 수행한다.
	First->EndLocalHoldAttempt(Character, true);
	const int32 PreviousAttemptId = First->BeginLocalHoldAttempt(Character);
	First->EndLocalHoldAttempt(Character, true);
	const int32 CurrentAttemptId = First->BeginLocalHoldAttempt(Character);
	Character->bAwaitingHoldInteractResult = true;
	Character->ClientOnHoldInteractFailed_Implementation(First, PreviousAttemptId);
	TestTrue(TEXT("Old failure preserves the retry"), Character->HoldingInteractActor == First);
	Character->ClientOnInteractSucceeded_Implementation(First, EInteractionFlowResult::Completed, PreviousAttemptId);
	TestTrue(TEXT("Old success cannot clear the retry"), Character->bAwaitingHoldInteractResult);
	Character->ClientOnInteractSucceeded_Implementation(First, EInteractionFlowResult::HoldReady, 0);
	TestTrue(TEXT("Late HoldReady cannot clear a newer hold"), Character->bAwaitingHoldInteractResult);
	Character->ClientOnHoldInteractFailed_Implementation(First, CurrentAttemptId);
	TestNull(TEXT("Current failure clears the input"), Character->HoldingInteractActor.Get());
	TestEqual(TEXT("Current failure clears the node's local attempt"), First->GetLocalHoldAttemptId(Character), 0);
	TestFalse(TEXT("Current failure exits result wait"), Character->bAwaitingHoldInteractResult);

	// 수집 함수가 먼저 bCollected를 설정해도 뒤따르는 기존 Hold commit은 성공해야 한다.
	First->bCollected = true;
	UInteractableComponent* Component = First->GetInteractableComponent();
	Component->HoldSessions.AddDefaulted_GetRef().Interactor = Character;
	TestTrue(TEXT("Collection preserves the server hold validation until commit"), Component->CanCommitHoldInteraction(Character));
	TestTrue(TEXT("Accepted collection commits the existing session"), Component->CommitHoldInteraction(Character));
	TestTrue(TEXT("Commit removes the server session"), Component->HoldSessions.IsEmpty());
	TestFalse(TEXT("Commit clears holding presentation"), First->HoldState.bHolding);

	// 실제 프로젝트 MI를 공유하는 두 메시에도 MID는 각각 생성돼야 한다.
	UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	UMaterialInterface* Material = LoadObject<UMaterialInterface>(nullptr,
		TEXT("/Game/Environments/Props/Materials/MI_NeuralCore.MI_NeuralCore"));
	if (!TestNotNull(TEXT("Test mesh exists"), Cube) || !TestNotNull(TEXT("NeuralCore material exists"), Material)) return false;
	for (AInteractionNode* Node : {First, Second})
	{
		UStaticMeshComponent* Mesh = NewObject<UStaticMeshComponent>(Node);
		Node->AddInstanceComponent(Mesh);
		Mesh->SetStaticMesh(Cube);
		Mesh->SetMaterial(0, Material);
		Mesh->RegisterComponent();
		// 첫 노드는 명시적 연결, 두 번째 노드는 BP와 같은 소유 메시 자동 연결 경로를 검증한다.
		if (Node == First) Node->EmissiveMesh.OverrideComponent = Mesh;
		Node->bUseMaterialIdleStrength = false;
		Node->IdleEmissiveStrength = 2.0f;
		Node->InitializeEmissiveMaterial();
		Node->bPresentationInitialized = true;
	}
	if (!TestNotNull(TEXT("First MID created"), First->EmissiveMID.Get())
		|| !TestNotNull(TEXT("Second MID created"), Second->EmissiveMID.Get())) return false;
	TestTrue(TEXT("Nodes have independent MIDs"), First->EmissiveMID != Second->EmissiveMID);
	TestTrue(TEXT("Unconfigured reference automatically resolves the owned mesh"),
		Second->CachedEmissiveMesh == Second->FindComponentByClass<UStaticMeshComponent>());
	Second->UpdateHoldPresentation();
	First->UpdateHoldPresentation();
	auto Strength = [](AInteractionNode* Node)
	{
		float Value = -1.0f;
		Node->EmissiveMID->GetScalarParameterValue(FMaterialParameterInfo(Node->EmissiveParameterName), Value);
		return Value;
	};
	TestEqual(TEXT("Collected material is off"), Strength(First), 0.0f);
	TestEqual(TEXT("Other node retains idle brightness"), Strength(Second), 2.0f);
	First->bCollected = false;
	First->HoldState.bHolding = true;
	First->HoldState.AttemptId = 8;
	First->HoldState.Duration = 2.0f;
	// 기본 커브는 초반에 빠르게 감소하며 완료 응답 대기 중에도 0을 유지한다.
	for (float Progress : {0.0f, 0.25f, 0.5f, 0.75f, 1.0f})
	{
		First->HoldState.ServerStartTime = World->GetTimeSeconds() - Progress * First->HoldState.Duration;
		First->UpdateHoldPresentation();
		const float Remaining = 1.0f - Progress;
		TestEqual(FString::Printf(TEXT("Progress %.2f follows the default cubic fade"), Progress),
			Strength(First), 2.0f * Remaining * Remaining * Remaining);
	}
	TestFalse(TEXT("Fully faded node stops presentation updates while awaiting collection"), First->NeedsPresentationUpdate());
	First->HoldState.bHolding = false;
	First->UpdateHoldPresentation();
	TestEqual(TEXT("Cancellation immediately restores idle brightness"), Strength(First), 2.0f);

	// 외부 커브도 같은 경로를 사용하되 잘못된 범위나 끝점이 상태 규칙을 덮어쓰지 못한다.
	UCurveFloat* CustomCurve = NewObject<UCurveFloat>(First);
	CustomCurve->FloatCurve.AddKey(0.0f, 2.0f);
	CustomCurve->FloatCurve.AddKey(1.0f, -1.0f);
	First->EmissiveFadeCurve.ExternalCurve = CustomCurve;
	TestEqual(TEXT("External curve is evaluated"), First->GetEmissiveStrength(0.5f), 1.0f);
	TestEqual(TEXT("Curve overshoot is clamped to idle"), First->GetEmissiveStrength(0.1f), 2.0f);
	TestEqual(TEXT("Negative curve strength is clamped to zero"), First->GetEmissiveStrength(0.9f), 0.0f);
	TestEqual(TEXT("Idle overrides a custom curve endpoint"), First->GetEmissiveStrength(0.0f), 2.0f);
	TestEqual(TEXT("Completion overrides a custom curve endpoint"), First->GetEmissiveStrength(1.0f), 0.0f);
	CustomCurve->FloatCurve.Reset();
	TestEqual(TEXT("Empty curve falls back to cubic fade"), First->GetEmissiveStrength(0.5f), 0.25f);
	CustomCurve->FloatCurve.AddKey(0.5f, std::numeric_limits<float>::quiet_NaN());
	TestEqual(TEXT("Non-finite curve falls back to cubic fade"), First->GetEmissiveStrength(0.5f), 0.25f);
	First->bCollected = true;
	TestEqual(TEXT("Collection overrides the curve"), First->GetEmissiveStrength(0.0f), 0.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FInteractionNodeHoldAttemptTest, "Outlier.Interaction.Node.HoldAttempts",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FInteractionNodeHoldAttemptTest::RunTest(const FString& Parameters)
{
	if (!TestNotNull(TEXT("Engine exists"), GEngine)) return false;
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false,
		MakeUniqueObjectName(nullptr, UWorld::StaticClass(), TEXT("NodeHoldAttemptTest")));
	if (!TestNotNull(TEXT("World exists"), World)) return false;
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());
	ON_SCOPE_EXIT
	{
		World->DestroyWorld(false);
		GEngine->DestroyWorldContext(World);
		World->RemoveFromRoot();
	};

	AInteractionNode* First = World->SpawnActor<AInteractionNode>();
	AInteractionNode* Second = World->SpawnActor<AInteractionNode>();
	AFirstPersonCharacter* Character = World->SpawnActor<AFirstPersonCharacter>();
	AFirstPersonCharacter* OtherCharacter = World->SpawnActor<AFirstPersonCharacter>();
	if (!TestNotNull(TEXT("First node exists"), First) || !TestNotNull(TEXT("Second node exists"), Second)
		|| !TestNotNull(TEXT("Requester exists"), Character) || !TestNotNull(TEXT("Other requester exists"), OtherCharacter)) return false;

	// Listen Server처럼 한 객체에서 로컬 시작과 서버 승인을 순서대로 처리한다.
	const int32 FirstAttempt = First->BeginLocalHoldAttempt(Character);
	TestTrue(TEXT("Node issues a positive attempt"), FirstAttempt > 0);
	TestEqual(TEXT("Local attempt belongs to its requester"), First->GetLocalHoldAttemptId(OtherCharacter), 0);
	TestTrue(TEXT("Server accepts first request"), First->AcceptServerHoldAttempt(Character, FirstAttempt));
	First->BeginHoldPresentation(Character, FirstAttempt, 2.0f);
	TestEqual(TEXT("Server approval preserves the local request"), First->GetLocalHoldAttemptId(Character), FirstAttempt);
	TestFalse(TEXT("Duplicate start is rejected"), First->AcceptServerHoldAttempt(Character, FirstAttempt));
	TestFalse(TEXT("Zero is not a server hold request"), First->AcceptServerHoldAttempt(Character, 0));
	TestTrue(TEXT("Different requester may use the same number"), First->AcceptServerHoldAttempt(OtherCharacter, FirstAttempt));
	First->EndLocalHoldAttempt(OtherCharacter, true);
	TestEqual(TEXT("Other requester cannot clear local attempt"), First->GetLocalHoldAttemptId(Character), FirstAttempt);

	// A를 취소하고 B로 이동하면 번호가 같아도 A의 응답은 B의 입력/UI에 영향을 주면 안 된다.
	Character->HoldingInteractActor = First;
	Character->CancelLocalHoldInteract(false);
	const int32 SecondNodeAttempt = Second->BeginLocalHoldAttempt(Character);
	TestEqual(TEXT("Nodes issue numbers independently"), SecondNodeAttempt, FirstAttempt);
	Character->HoldingInteractActor = Second;
	Character->FocusedInteractable = Second;
	Character->bAwaitingHoldInteractResult = true;
	Character->ClientOnHoldInteractFailed_Implementation(First, FirstAttempt);
	Character->ClientOnInteractSucceeded_Implementation(First, EInteractionFlowResult::Completed, FirstAttempt);
	Character->ClientOnInteractSucceeded_Implementation(First, EInteractionFlowResult::HoldReady, 0);
	TestTrue(TEXT("Previous node responses preserve the current target"), Character->HoldingInteractActor == Second);
	TestTrue(TEXT("Previous HoldReady preserves current focus"), Character->FocusedInteractable == Second);
	TestTrue(TEXT("Previous responses preserve result wait"), Character->bAwaitingHoldInteractResult);

	// 원격 클라이언트에서도 수집/세션 종료 복제가 성공 RPC보다 먼저 도착할 수 있다.
	Second->SetRole(ROLE_AutonomousProxy);
	Second->BeginHoldPresentation(Character, SecondNodeAttempt, 2.0f);
	Second->EndHoldPresentation(false);
	TestEqual(TEXT("Replicated completion retains the local response number"),
		Second->GetLocalHoldAttemptId(Character), SecondNodeAttempt);
	Second->bCollected = true;
	Character->ClientOnInteractSucceeded_Implementation(Second, EInteractionFlowResult::Completed, SecondNodeAttempt);
	Second->SetRole(ROLE_Authority);
	TestNull(TEXT("Current success clears the input after collection"), Character->HoldingInteractActor.Get());
	TestEqual(TEXT("Current success clears the local attempt"), Second->GetLocalHoldAttemptId(Character), 0);
	TestFalse(TEXT("Current success ends result wait"), Character->bAwaitingHoldInteractResult);

	// 서버 진행 상태가 먼저 지워져도 무효화 통지에는 원래 번호가 필요하다.
	const int32 Retry = First->BeginLocalHoldAttempt(Character);
	TestTrue(TEXT("Retry advances the node's number"), Retry > FirstAttempt);
	TestTrue(TEXT("Server accepts retry"), First->AcceptServerHoldAttempt(Character, Retry));
	First->GetInteractableComponent()->HoldSessions.AddDefaulted_GetRef().Interactor = Character;
	First->BeginHoldPresentation(Character, Retry, 2.0f);
	Character->ServerCancelHoldInteract_Implementation(First, FirstAttempt);
	TestEqual(TEXT("Old cancel preserves the retry's hold session"), First->GetInteractableComponent()->HoldSessions.Num(), 1);
	TestTrue(TEXT("Old cancel preserves the new presentation"), First->HoldState.bHolding);
	TestFalse(TEXT("Old completion does not match the retry"), First->IsServerHoldAttemptCurrent(Character, FirstAttempt));
	First->EndHoldPresentation(true);
	TestEqual(TEXT("Presentation end retains server response number"), First->GetServerHoldAttemptId(Character), Retry);
	TestEqual(TEXT("Presentation end retains local response wait"), First->GetLocalHoldAttemptId(Character), Retry);
	First->SetRole(ROLE_AutonomousProxy);
	First->EndHoldPresentation(true);
	TestEqual(TEXT("Replicated cancellation also retains the local response number"), First->GetLocalHoldAttemptId(Character), Retry);
	TestEqual(TEXT("Cancelled snapshot cannot resume progress"), First->GetHoldProgress(), 0.0f);
	First->SetRole(ROLE_Authority);
	TestFalse(TEXT("Older start remains rejected after cancellation"), First->AcceptServerHoldAttempt(Character, FirstAttempt));
	Character->HoldingInteractActor = First;
	Character->bAwaitingHoldInteractResult = true;
	Character->ClientOnHoldInteractFailed_Implementation(First, Retry);
	TestEqual(TEXT("Current invalidation clears local attempt"), First->GetLocalHoldAttemptId(Character), 0);
	TestNull(TEXT("Current invalidation clears input"), Character->HoldingInteractActor.Get());

	Character->Destroy();
	AFirstPersonCharacter* Respawned = World->SpawnActor<AFirstPersonCharacter>();
	if (!TestNotNull(TEXT("Respawned requester exists"), Respawned)) return false;
	TestEqual(TEXT("Respawn has no inherited local request"), First->GetLocalHoldAttemptId(Respawned), 0);
	TestTrue(TEXT("Respawn starts its own server sequence"), First->AcceptServerHoldAttempt(Respawned, 1));
	TestEqual(TEXT("Other requester's watermark survives cleanup"), First->GetServerHoldAttemptId(OtherCharacter), FirstAttempt);
	First->BeginLocalHoldAttempt(Respawned);
	First->EndPlay(EEndPlayReason::RemovedFromWorld);
	TestTrue(TEXT("EndPlay releases request records"), First->LatestServerAttemptIds.IsEmpty());
	TestEqual(TEXT("EndPlay clears local state"), First->LocalAttemptId, 0);
	return true;
}

#endif
