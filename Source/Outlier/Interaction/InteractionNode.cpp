#include "Interaction/InteractionNode.h"
#include "Audio/OutlierAudioFeedback.h"

#include "Audio/OutlierAudioSubsystem.h"
#include "Components/SceneComponent.h"
#include "Components/MeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "GameFramework/GameStateBase.h"
#include "GameplayTags/OutlierGameplayTags.h"
#include "FirstPerson/FirstPersonCharacter.h"
#include "Interaction/InteractableComponent.h"
#include "OutlierPlayerState.h"
#include "Save/OutlierSaveSubSystem.h"
#include "Engine/World.h"
#include "Net/UnrealNetwork.h"

AInteractionNode::AInteractionNode()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	RootComponent = SceneRoot;

	InteractableComponent = CreateDefaultSubobject<UInteractableComponent>(TEXT("InteractableComponent"));
	// BP에서는 이 기준점의 상대 위치만 조절한다. 별도 UI 위치 보정은 적용하지 않는다.
	InteractionTargetPoint = CreateDefaultSubobject<USceneComponent>(TEXT("InteractionTargetPoint"));
	InteractionTargetPoint->SetupAttachment(GetRootComponent());
	InteractionTargetPoint->bEditableWhenInherited = true;
	InteractableComponent->InteractionTargetPointReference.ComponentProperty = GET_MEMBER_NAME_CHECKED(AInteractionNode, InteractionTargetPoint);

	// 기본 곡선은 (1 - 진행률)^3이다. 초반에 빠르게 낮아져 진행 중 밝기 차이를 드러낸다.
	FRichCurve* Curve = EmissiveFadeCurve.GetRichCurve();
	const FKeyHandle StartKey = Curve->AddKey(0.0f, 1.0f);
	const FKeyHandle EndKey = Curve->AddKey(1.0f, 0.0f);
	for (FKeyHandle Key : {StartKey, EndKey})
	{
		Curve->SetKeyInterpMode(Key, RCIM_Cubic);
		Curve->SetKeyTangentMode(Key, RCTM_User);
	}
	Curve->GetKey(StartKey).ArriveTangent = -3.0f;
	Curve->GetKey(StartKey).LeaveTangent = -3.0f;
	Curve->GetKey(EndKey).ArriveTangent = 0.0f;
	Curve->GetKey(EndKey).LeaveTangent = 0.0f;
}

void AInteractionNode::BeginPlay()
{
	Super::BeginPlay();
	PresentationClock = GetWorld()->GetGameState();
	InitializeEmissiveMaterial();
	bPresentationInitialized = true;
	UpdateHoldPresentation();
	OutlierAudioFeedback::Loop(this, TEXT("Audio.Context.Object.Node.Indicate"), !bCollected);

	if (!HasAuthority())
	{
		return;
	}
	if (UOutlierSaveSubSystem* SaveSubsystem = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UOutlierSaveSubSystem>()
		: nullptr)
	{
		bProgressIdRegistered = SaveSubsystem->RegisterWorldProgressId(
			EOutlierWorldProgressType::CollectedNode,
			PickupId,
			this);
		if (bProgressIdRegistered
			&& SaveSubsystem->HasWorldProgress(EOutlierWorldProgressType::CollectedNode, PickupId))
		{
			// 저장된 습득 상태만 복원한다. AddNodeServer를 호출하면 이미 저장한 재화를 중복 지급한다.
			bCollected = true;
			PublishHoldState(false);
			OutlierAudioFeedback::StopLoops(this);
			InteractableComponent->RestoreUsedState(true);
			OnCollectedStateChanged(true);
			ForceNetUpdate();
		}
	}
}

void AInteractionNode::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	bPresentationInitialized = false;
	bPresentationUpdating = false;
	EmissiveMID = nullptr;
	CachedEmissiveMesh = nullptr;
	PresentationClock = nullptr;
	LocalAttemptId = 0;
	CanceledAttemptId = 0;
	LocalInteractor.Reset();
	CanceledInteractor.Reset();
	LatestServerAttemptIds.Reset();
	OutlierAudioFeedback::StopLoops(this);
	if (bProgressIdRegistered)
	{
		if (UOutlierSaveSubSystem* SaveSubsystem = GetGameInstance()
			? GetGameInstance()->GetSubsystem<UOutlierSaveSubSystem>()
			: nullptr)
		{
			SaveSubsystem->UnregisterWorldProgressId(
				EOutlierWorldProgressType::CollectedNode,
				PickupId,
				this);
		}
	}
	Super::EndPlay(EndPlayReason);
}

UInteractableComponent* AInteractionNode::GetInteractableComponent() const
{
	return InteractableComponent;
}

bool AInteractionNode::Interact(AFirstPersonCharacter* Interactor)
{
	if (!HasAuthority() || !Interactor || !InteractableComponent || bCollected
		|| !Interactor->GetOwnedGameplayTagsForQuery().HasTagExact(OutlierGameplayTags::Actor::Role::Partner()))
	{
		return false;
	}

	const EInteractionFlowResult FlowResult =
		InteractableComponent->AdvanceInteractionFlow(Interactor);

	if (FlowResult == EInteractionFlowResult::Rejected)
	{
		UE_LOG(LogTemp, Warning, TEXT("[InteractionNode] Interaction flow rejected Actor=%s"), *GetName());
		return false;
	}

	if (FlowResult == EInteractionFlowResult::HoldReady)
	{
		return true;
	}

	return AddNodeServer(Interactor);
}

bool AInteractionNode::AddNodeServer(AFirstPersonCharacter* Interactor)
{
	if (!HasAuthority())
	{
		return false;
	}

	AOutlierPlayerState* PlayerState =
		Interactor ? Interactor->GetPlayerState<AOutlierPlayerState>() : nullptr;

	if (!PlayerState)
	{
		return false;
	}

	if (!PlayerState->ShareNode(NodeRewardAmount))
	{
		return false;
	}
	UOutlierAudioSubsystem::PlayTaggedAtLocationFromServer(
		this,
		FGameplayTag::RequestGameplayTag(TEXT("Audio.Type.Interactable")),
		FGameplayTag::RequestGameplayTag(TEXT("Audio.Context.Object.Node.Acquire")));

	if (bProgressIdRegistered)
	{
		if (UOutlierSaveSubSystem* SaveSubsystem = GetGameInstance()
			? GetGameInstance()->GetSubsystem<UOutlierSaveSubSystem>()
			: nullptr)
		{
			SaveSubsystem->SetWorldProgressState(
				EOutlierWorldProgressType::CollectedNode,
				PickupId,
				true);
		}
	}
	bCollected = true;
	// Character는 이 함수 성공 뒤 Hold 세션을 검증하고 확정한다. 시작 시각은 그때까지 유지한다.
	UpdateHoldPresentation();
	OutlierAudioFeedback::StopLoops(this);
	OnCollectedStateChanged(true);
	ForceNetUpdate();

	/*UE_LOG(LogTemp, Verbose,
		TEXT("[InteractionNode] Shared node reward Player=%s Amount=%d PlayerTotal=%d"),
		*PlayerState->GetPlayerName(),
		NodeRewardAmount,
		PlayerState->GetNodeCount());*/

	return true;
}

void AInteractionNode::OnRep_Collected()
{
	// 수집 확정은 늦게 도착한 진행/취소 상태보다 우선한다.
	UpdateHoldPresentation();
	OutlierAudioFeedback::Loop(this, TEXT("Audio.Context.Object.Node.Indicate"), !bCollected);
	OnCollectedStateChanged(bCollected);
}

void AInteractionNode::GetLifetimeReplicatedProps(
	TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AInteractionNode, bCollected);
	DOREPLIFETIME(AInteractionNode, HoldState);
}

float FInteractionNodeHoldState::GetProgress(double ServerTime) const
{
	if (!bHolding || !FMath::IsFinite(Duration) || Duration <= 0.0f
		|| !FMath::IsFinite(ServerTime) || !FMath::IsFinite(ServerStartTime)) return 0.0f;
	return static_cast<float>(FMath::Clamp((ServerTime - ServerStartTime) / Duration, 0.0, 1.0));
}

int32 AInteractionNode::BeginLocalHoldAttempt(AFirstPersonCharacter* Interactor)
{
	if (!IsValid(Interactor) || bCollected || NextLocalAttemptId == MAX_int32) return 0;
	LocalAttemptId = ++NextLocalAttemptId;
	LocalInteractor = Interactor;
	return LocalAttemptId;
}

int32 AInteractionNode::GetLocalHoldAttemptId(const AFirstPersonCharacter* Interactor) const
{
	return Interactor && LocalInteractor.Get() == Interactor ? LocalAttemptId : 0;
}

void AInteractionNode::EndLocalHoldAttempt(const AFirstPersonCharacter* Interactor, bool bCanceled)
{
	if (LocalInteractor.Get() != Interactor) return;
	if (bCanceled && LocalAttemptId != 0)
	{
		CanceledAttemptId = LocalAttemptId;
		CanceledInteractor = LocalInteractor;
	}
	LocalAttemptId = 0;
	LocalInteractor.Reset();
	UpdateHoldPresentation();
}

bool AInteractionNode::AcceptServerHoldAttempt(AFirstPersonCharacter* Interactor, int32 AttemptId)
{
	if (!HasAuthority() || !IsValid(Interactor) || AttemptId <= 0) return false;
	// 리스폰 전 Pawn의 기록은 정리한다. 살아 있는 Pawn의 번호는 취소 후에도 남겨 오래된 시작을 거절한다.
	for (auto It = LatestServerAttemptIds.CreateIterator(); It; ++It)
	{
		if (!It.Key().IsValid()) It.RemoveCurrent();
	}
	int32& LatestAttemptId = LatestServerAttemptIds.FindOrAdd(Interactor);
	if (AttemptId <= LatestAttemptId) return false;
	LatestAttemptId = AttemptId;
	return true;
}

int32 AInteractionNode::GetServerHoldAttemptId(const AFirstPersonCharacter* Interactor) const
{
	const int32* AttemptId = Interactor ? LatestServerAttemptIds.Find(Interactor) : nullptr;
	// 진행 snapshot이 종료된 뒤에도 무효화/결과 통지는 원래 요청 번호로 보낸다.
	return AttemptId ? *AttemptId : 0;
}

bool AInteractionNode::IsServerHoldAttemptCurrent(const AFirstPersonCharacter* Interactor, int32 AttemptId) const
{
	return AttemptId > 0 && GetServerHoldAttemptId(Interactor) == AttemptId;
}

double AInteractionNode::GetPresentationTime() const
{
	if (IsValid(PresentationClock)) return PresentationClock->GetServerWorldTimeSeconds();
	// 클라이언트 로컬 시각으로 진행을 추측하지 않는다. 서버 시계가 준비되면 다시 계산한다.
	const UWorld* World = GetWorld();
	return HasAuthority() && World ? World->GetTimeSeconds() : HoldState.ServerStartTime;
}

void AInteractionNode::BeginHoldPresentation(AFirstPersonCharacter* Interactor, int32 AttemptId, float Duration)
{
	// 템플릿이나 월드가 없는 객체에서는 서버 진행 상태를 만들지 않는다.
	if (bCollected || IsTemplate() || !GetWorld()) return;
	if (HasAuthority())
	{
		PublishHoldState(true, AttemptId, Duration, Interactor);
	}
	else
	{
		// BeginLocalHoldAttempt에서 발급한 번호로 서버 승인을 기다린다.
		UpdateHoldPresentation();
	}
}

void AInteractionNode::EndHoldPresentation(bool bCanceled)
{
	if (HasAuthority())
	{
		PublishHoldState(false);
	}
	else
	{
		// Used/취소 복제가 결과 RPC보다 먼저 올 수 있다. 표시는 종료하되 요청 번호는 응답 처리까지 보존한다.
		if (bCanceled && LocalAttemptId != 0)
		{
			CanceledAttemptId = LocalAttemptId;
			CanceledInteractor = LocalInteractor;
		}
		UpdateHoldPresentation();
	}
}

void AInteractionNode::PublishHoldState(bool bHolding, int32 AttemptId, float Duration,
	AFirstPersonCharacter* Interactor)
{
	if (!HasAuthority()) return;
	++HoldState.Revision;
	HoldState.bHolding = bHolding;
	HoldState.AttemptId = AttemptId;
	HoldState.Interactor = Interactor;
	HoldState.ServerStartTime = GetPresentationTime();
	HoldState.Duration = Duration;
	UpdateHoldPresentation();
	ForceNetUpdate();
}

float AInteractionNode::GetHoldProgress() const
{
	// 번호는 노드별로 발급하며, 진행 snapshot은 상호작용한 Pawn까지 함께 구분한다.
	const bool bCanceled = CanceledAttemptId != 0 && HoldState.AttemptId == CanceledAttemptId
		&& HoldState.Interactor == CanceledInteractor.Get();
	const bool bWaitingForApproval = LocalAttemptId != 0
		&& (HoldState.AttemptId != LocalAttemptId || HoldState.Interactor != LocalInteractor.Get());
	if (bCollected || bCanceled || bWaitingForApproval) return 0.0f;
	return HoldState.GetProgress(GetPresentationTime());
}

bool AInteractionNode::NeedsPresentationUpdate() const
{
	return bPresentationUpdating;
}

void AInteractionNode::OnRep_HoldState()
{
	UpdateHoldPresentation();
}

float AInteractionNode::GetEmissiveStrength(float Progress) const
{
	// 커브를 편집해도 취소/대기와 완료 상태의 밝기는 항상 보장한다.
	if (bCollected || Progress >= 1.0f) return 0.0f;
	if (Progress <= 0.0f) return ResolvedIdleStrength;
	const float Remaining = 1.0f - Progress;
	const float DefaultScale = Remaining * Remaining * Remaining;
	const FRichCurve* Curve = EmissiveFadeCurve.GetRichCurveConst();
	const float Scale = Curve && !Curve->IsEmpty() ? Curve->Eval(Progress) : DefaultScale;
	return ResolvedIdleStrength * (FMath::IsFinite(Scale) ? FMath::Clamp(Scale, 0.0f, 1.0f) : DefaultScale);
}

void AInteractionNode::UpdateHoldPresentation()
{
	if (!bPresentationInitialized) return;
	if (!IsValid(PresentationClock)) PresentationClock = GetWorld()->GetGameState();
	const float Progress = GetHoldProgress();
	const bool bCanceled = CanceledAttemptId != 0 && HoldState.AttemptId == CanceledAttemptId
		&& HoldState.Interactor == CanceledInteractor.Get();
	// 마지막 100% 값을 적용한 뒤 갱신을 끈다. 대기/수집 중에는 MID를 반복 갱신하지 않는다.
	bPresentationUpdating = GetNetMode() != NM_DedicatedServer && !bCollected && HoldState.bHolding
		&& Progress < 1.0f && !bCanceled;
	if (IsValid(EmissiveMID) && IsValid(CachedEmissiveMesh))
	{
		EmissiveMID->SetScalarParameterValue(EmissiveParameterName, GetEmissiveStrength(Progress));
	}
	InteractableComponent->RefreshWidgetTick();
}

void AInteractionNode::InitializeEmissiveMaterial()
{
	if (GetNetMode() == NM_DedicatedServer) return;
	CachedEmissiveMesh = Cast<UMeshComponent>(EmissiveMesh.GetComponent(this));
	const bool bMeshExplicitlyConfigured = !EmissiveMesh.ComponentProperty.IsNone()
		|| !EmissiveMesh.PathToComponent.IsEmpty() || EmissiveMesh.OverrideComponent.IsValid()
		|| EmissiveMesh.OtherActor.IsValid();
	if (!CachedEmissiveMesh && !bMeshExplicitlyConfigured && EmissiveMaterialIndex >= 0
		&& !EmissiveParameterName.IsNone())
	{
		// BP에서 추가한 메시도 BeginPlay에는 생성되어 있다. 검색은 초기화 때 한 번만 수행한다.
		TInlineComponentArray<UMeshComponent*> Meshes;
		GetComponents(Meshes);
		for (UMeshComponent* Mesh : Meshes)
		{
			if (EmissiveMaterialIndex >= Mesh->GetNumMaterials()) continue;
			UMaterialInterface* CandidateMaterial = Mesh->GetMaterial(EmissiveMaterialIndex);
			float CandidateStrength = 0.0f;
			if (!CandidateMaterial || !CandidateMaterial->GetScalarParameterValue(
				FMaterialParameterInfo(EmissiveParameterName), CandidateStrength)) continue;
			if (CachedEmissiveMesh)
			{
				// 복수 후보의 컴포넌트 순서에 의존하지 않는다. 이 경우에는 대상을 명시해야 한다.
				CachedEmissiveMesh = nullptr;
				UE_LOG(LogTemp, Warning, TEXT("[InteractionNode] Multiple emissive meshes found; specify EmissiveMesh Actor=%s"), *GetName());
				return;
			}
			CachedEmissiveMesh = Mesh;
		}
	}
	if (!CachedEmissiveMesh || EmissiveMaterialIndex < 0
		|| EmissiveMaterialIndex >= CachedEmissiveMesh->GetNumMaterials() || EmissiveParameterName.IsNone())
	{
		UE_LOG(LogTemp, Warning, TEXT("[InteractionNode] Emissive mesh/slot is not configured Actor=%s"), *GetName());
		return;
	}
	UMaterialInterface* Material = CachedEmissiveMesh->GetMaterial(EmissiveMaterialIndex);
	float MaterialIdleStrength = 0.0f;
	if (!Material || !Material->GetScalarParameterValue(FMaterialParameterInfo(EmissiveParameterName), MaterialIdleStrength))
	{
		UE_LOG(LogTemp, Warning, TEXT("[InteractionNode] Emissive parameter missing Actor=%s Parameter=%s"),
			*GetName(), *EmissiveParameterName.ToString());
		return;
	}
	if (!FMath::IsFinite(MaterialIdleStrength) || !FMath::IsFinite(IdleEmissiveStrength))
	{
		UE_LOG(LogTemp, Warning, TEXT("[InteractionNode] Emissive strength must be finite Actor=%s"), *GetName());
		return;
	}
	ResolvedIdleStrength = FMath::Max(0.0f, bUseMaterialIdleStrength ? MaterialIdleStrength : IdleEmissiveStrength);
	EmissiveMID = CachedEmissiveMesh->CreateDynamicMaterialInstance(EmissiveMaterialIndex);
}
