// Fill out your copyright notice in the Description page of Project Settings.


#include "FirstPersonCharacter.h"
#include "Logging/OutlierLog.h"
#include "Animation/AnimInstance.h"
#include "Audio/OutlierAudioSubsystem.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "DrawDebugHelpers.h"
#include "FirstPersonInputConfig.h"
#include "Interface/InteractableInterface.h"
#include "Interaction/InteractableComponent.h"
#include "Interaction/InteractionNode.h"
#include "FirstPerson/FirstPersonPlayerController.h"
#include "EnhancedInputComponent.h"
#include "Engine/LocalPlayer.h"
#include "Engine/GameInstance.h"
#include "InputAction.h"
#include "InputActionValue.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Weapon/WeaponBase.h"
#include "PostProcess/OutlierStencilSubsystem.h"
#include "Weapon/FirstPickupDiagnostics.h"
#include "Net/UnrealNetwork.h"
#include "Engine/OverlapResult.h"
#include "OutlierNetUtils.h"
#include "Outlier.h"
#include "Shooter/ShooterCharacter.h"
#include "Team/OutlierTeamIds.h"
#include "Room/RoomTagComponent.h"
#include "Settings/LocalPlayerSettingsSubsystem.h"
#include "LocalPlayerUISubSystem.h"
#include "Enemy/EnemyRoomSubsystem.h"
#include "Network/OutlierArenaSubsystem.h"
#include "EngineUtils.h"
#include "Engine/Level.h"
#include "Drone/Partner/PartnerCharacter.h"
#include "Drone/Partner/PartnerHackComponent.h"
#include "Drone/Partner/PartnerEMPComponent.h"
#include "GameplayTags/OutlierGameplayTags.h"
#include "Interaction/ActorBodyTargeting.h"


// Sets default values
AFirstPersonCharacter::AFirstPersonCharacter(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Set size for collision capsule
	GetCapsuleComponent()->InitCapsuleSize(55.0f, 96.0f);

	FirstPersonCameraRoot = CreateDefaultSubobject<USceneComponent>(TEXT("First Person Camera Root"));
	FirstPersonCameraRoot->SetupAttachment(GetCapsuleComponent());
	FirstPersonCameraRoot->SetRelativeLocation(FVector(0.0f, 0.0f, 64.0f));

	// Create Camera
	FirstPersonCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("First Person Camera"));
	FirstPersonCamera->SetupAttachment(FirstPersonCameraRoot);
	FirstPersonCamera->SetRelativeLocationAndRotation(FVector::ZeroVector, FRotator::ZeroRotator);
	FirstPersonCamera->bUsePawnControlRotation = true;
	FirstPersonCamera->bEnableFirstPersonFieldOfView = true;
	FirstPersonCamera->bEnableFirstPersonScale = false;
	FirstPersonCamera->FirstPersonFieldOfView = 70.0f;
	FirstPersonCamera->FirstPersonScale = 1.0f;

	FirstPersonViewModelRoot = CreateDefaultSubobject<USceneComponent>(TEXT("First Person ViewModel Root"));
	FirstPersonViewModelRoot->SetupAttachment(FirstPersonCamera);
	FirstPersonViewModelRoot->SetRelativeLocation(FVector(0.0f, 0.0f, 0.0f));
	FirstPersonViewModelRoot->SetRelativeRotation(FRotator::ZeroRotator);

	FirstPersonMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("First Person Mesh"));
	FirstPersonMesh->SetupAttachment(FirstPersonViewModelRoot);
	FirstPersonMesh->SetOnlyOwnerSee(true);
	FirstPersonMesh->FirstPersonPrimitiveType = EFirstPersonPrimitiveType::FirstPerson;
	FirstPersonMesh->SetRelativeLocation(FVector(-2.0f, 0.0f, -130.0f));
	FirstPersonMesh->SetRelativeRotation(FRotator::ZeroRotator);
	FirstPersonMesh->SetCollisionProfileName(FName("NoCollision"));

	RoomTagComponent = CreateDefaultSubobject<URoomTagComponent>(TEXT("RoomTagComponent"));

	// configure the character comps
	GetMesh()->SetOwnerNoSee(true);
	GetMesh()->FirstPersonPrimitiveType = EFirstPersonPrimitiveType::WorldSpaceRepresentation;
	GetMesh()->SetWorldLocation(FVector(0, 0, -GetCapsuleComponent()->GetScaledCapsuleHalfHeight()));

	GetCapsuleComponent()->SetCapsuleSize(34.0f, 96.0f);

	// configure character movement
	// 공중 자동 제동은 적용하지 않는다. Slide 점프의 초과 속도 감속은 전용 커브가 담당한다.
	GetCharacterMovement()->BrakingDecelerationFalling = 0.0f;
	GetCharacterMovement()->AirControl = 0.5f;
}

FGenericTeamId AFirstPersonCharacter::GetGenericTeamId() const
{
	return FGenericTeamId(OutlierTeamIds::Player);
}

URoomTagComponent* AFirstPersonCharacter::GetRoomTagComp() const
{
	return RoomTagComponent;
}

// Called to bind functionality to input
void AFirstPersonCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	// 로컬 빙의 전에 BeginPlay가 실행될 수 있으므로 입력 준비 시점에 설정을 바인딩한다.
	if (BoundSettingsSubsystem)
	{
		BoundSettingsSubsystem->OnMouseSensitivityChanged.RemoveAll(this);
		BoundSettingsSubsystem = nullptr;
	}
	MouseSensitivity = 1.0f;
	if (APlayerController* PlayerController = Cast<APlayerController>(GetController()))
	{
		if (ULocalPlayer* LocalPlayer = PlayerController->GetLocalPlayer())
		{
			BoundSettingsSubsystem = LocalPlayer->GetSubsystem<ULocalPlayerSettingsSubsystem>();
			if (BoundSettingsSubsystem)
			{
				MouseSensitivity = BoundSettingsSubsystem->GetMouseSensitivity();
				BoundSettingsSubsystem->OnMouseSensitivityChanged.AddUniqueDynamic(
					this, &AFirstPersonCharacter::HandleMouseSensitivityChanged);
			}
		}
	}

	UEnhancedInputComponent* EnhancedInputComponent = Cast<UEnhancedInputComponent>(PlayerInputComponent);
	if (!EnhancedInputComponent || !InputConfig) {
		UE_LOG(LogTemp, Warning, TEXT("EnhancedInputComponent or InputConfig is Null"));
		return;
	}

	// Move
	EnhancedInputComponent->BindAction(InputConfig->MoveAction, ETriggerEvent::Triggered, this, &AFirstPersonCharacter::MoveInput);
	EnhancedInputComponent->BindAction(InputConfig->MoveAction, ETriggerEvent::Completed, this, &AFirstPersonCharacter::StopMoveInput);
	EnhancedInputComponent->BindAction(InputConfig->MoveAction, ETriggerEvent::Canceled, this, &AFirstPersonCharacter::StopMoveInput);

	// Look
	EnhancedInputComponent->BindAction(InputConfig->LookAction, ETriggerEvent::Triggered, this, &AFirstPersonCharacter::LookInput);

	// Attack
	EnhancedInputComponent->BindAction(InputConfig->AttackAction, ETriggerEvent::Started, this, &AFirstPersonCharacter::TryStartAttack);
	EnhancedInputComponent->BindAction(InputConfig->AttackAction, ETriggerEvent::Completed, this, &AFirstPersonCharacter::TryStopAttack);

	// Interaction
	EnhancedInputComponent->BindAction(InputConfig->InteractionAction, ETriggerEvent::Started, this, &AFirstPersonCharacter::HandleInteractionInputStarted);
	EnhancedInputComponent->BindAction(InputConfig->InteractionAction, ETriggerEvent::Completed, this, &AFirstPersonCharacter::EndInteract);
	EnhancedInputComponent->BindAction(InputConfig->InteractionAction, ETriggerEvent::Canceled, this, &AFirstPersonCharacter::EndInteract);

	// EnhancedInputComponent->BindAction(InputConfig->DebugArenaReload, ETriggerEvent::Started, this, &AFirstPersonCharacter::ArenaReload);
}

void AFirstPersonCharacter::BeginPlay()
{
	Super::BeginPlay();
	if (HasAuthority())
	{
		PresentationEnemyRooms = GetWorld()->GetSubsystem<UEnemyRoomSubsystem>();
		PresentationArena = GetWorld()->GetSubsystem<UOutlierArenaSubsystem>();
		if (PresentationArena.IsValid())
		{
			PresentationArena->OnArenaGameplayReloadStarted.AddUObject(this, &ThisClass::ClearPresentationProtection);
		}
	}
	NormalizeInteractionSphereRanges();
	NodeBlockingHack = FindComponentByClass<UPartnerHackComponent>();
	NodeBlockingEMP = FindComponentByClass<UPartnerEMPComponent>();
	// 충돌 없는 노드도 후보에 포함한다. 초기 검색 뒤에는 생성/레벨 추가 알림으로만 갱신한다.
	for (TActorIterator<AInteractionNode> It(GetWorld()); It; ++It)
	{
		CacheInteractionNode(*It);
	}
	NodeSpawnedHandle = GetWorld()->AddOnActorSpawnedHandler(
		FOnActorSpawned::FDelegate::CreateUObject(this, &ThisClass::CacheInteractionNode));
	// 스트리밍으로 역직렬화된 배치 액터는 Spawn 알림을 거치지 않으므로 별도로 수집한다.
	FWorldDelegates::LevelAddedToWorld.AddWeakLambda(this, [this](ULevel* Level, UWorld* World)
	{
		if (World == GetWorld() && Level)
		{
			for (AActor* Actor : Level->Actors)
			{
				CacheInteractionNode(Actor);
			}
		}
	});

	GetWorldTimerManager().SetTimer(
		InteractionTraceTimerHandle,
		this,
		&AFirstPersonCharacter::UpdateInteractableFocus,
		InteractionTraceInterval,
		true
	);

	OUTLIER_LOG(Core,
		LogTemp,
		Log,
		TEXT("%s %s FPBeginPlay FirstPersonMesh=%s MeshAnimClass=%s MeshAnimInstance=%s"),
		OutlierNet::GetNetPrefix(this),
		*GetName(),
		*GetNameSafe(FirstPersonMesh),
		FirstPersonMesh ? *GetNameSafe(FirstPersonMesh->GetAnimClass()) : TEXT("None"),
		FirstPersonMesh ? *GetNameSafe(FirstPersonMesh->GetAnimInstance()) : TEXT("None"));

}

void AFirstPersonCharacter::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ClearPresentationProtection(0);
	if (PresentationArena.IsValid()) PresentationArena->OnArenaGameplayReloadStarted.RemoveAll(this);
	GetWorldTimerManager().ClearTimer(InteractionTraceTimerHandle);
	GetWorld()->RemoveOnActorSpawnedHandler(NodeSpawnedHandle);
	FWorldDelegates::LevelAddedToWorld.RemoveAll(this);
	InteractionNodes.Reset();
	ClearInteractionDetection();
	OnSuitTransitionParticipantInvalidated.Broadcast(this);
	OnSuitTransitionParticipantInvalidated.Clear();
	if (BoundSettingsSubsystem)
	{
		BoundSettingsSubsystem->OnMouseSensitivityChanged.RemoveAll(this);
		BoundSettingsSubsystem = nullptr;
	}

	Super::EndPlay(EndPlayReason);
}

void AFirstPersonCharacter::MoveInput(const FInputActionValue& Value)
{
	// get the Vector2D move axis
	const FVector2D MovementVector = Value.Get<FVector2D>();

	DoMove(MovementVector.X, MovementVector.Y);

	OnMoveInputUpdated(MovementVector);
}

void AFirstPersonCharacter::StopMoveInput()
{
	// 종료 이벤트가 마지막 축 값을 전달해도 저장된 방향을 다시 살리지 않는다.
	DoMove(0.0f, 0.0f);
	OnMoveInputUpdated(FVector2D::ZeroVector);
}

void AFirstPersonCharacter::ResetMovementInput()
{
	StopMoveInput();
	ConsumeMovementInputVector();
}

void AFirstPersonCharacter::LookInput(const FInputActionValue& Value)
{
	// get the Vector2D move axis
	const FVector2D LookAxisVector = Value.Get<FVector2D>();

	// 카메라 기준이라 Y를 뒤집음
	DoAim(
		LookAxisVector.X * MouseSensitivity,
		-LookAxisVector.Y * MouseSensitivity);
}

void AFirstPersonCharacter::HandleMouseSensitivityChanged(float NewValue)
{
	MouseSensitivity = NewValue;
}

void AFirstPersonCharacter::HandleInteractionInputStarted()
{
	PlayInteractionRelevantAtLocationAudio();
	TryInteract();
}

bool AFirstPersonCharacter::PlayInteractionRelevantAtLocationAudio()
{
	if (!InteractionAudioEventTag.IsValid())
	{
		return false;
	}

	UOutlierAudioSubsystem* AudioSubsystem = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UOutlierAudioSubsystem>()
		: nullptr;
	if (!AudioSubsystem)
	{
		return false;
	}

	FOutlierAudioPlayRequest Request;
	Request.EventTag = InteractionAudioEventTag;
	Request.ContextTags = InteractionAudioContextTags;
	Request.EmitterActor = this;
	Request.Location = GetActorLocation();
	Request.bHasLocation = true;

	return AudioSubsystem->PlayRelevantAtLocationFromOwningClient(Request);
}

void AFirstPersonCharacter::DoMove(float Right, float Forward)
{
	if (IsSuitTransitionBlocked())
	{
		return;
	}
	if (const AShooterCharacter* ShooterCharacter = Cast<AShooterCharacter>(this))
	{
		if (ShooterCharacter->GetMovementState() == EMovementState::Slide)
		{
			return;
		}
	}

	if (GetController())
	{
		// move inputs
		AddMovementInput(GetActorRightVector(), Right);
		AddMovementInput(GetActorForwardVector(), Forward);
	}
}

void AFirstPersonCharacter::DoAim(float Yaw, float Pitch)
{
	if (GetController())
	{
		// rotation inputs
		AddControllerYawInput(Yaw);
		AddControllerPitchInput(Pitch);
	}
}

bool AFirstPersonCharacter::CanInteract() const
{
	if (IsSuitTransitionBlocked())
	{
		return false;
	}
	return true;
}

void AFirstPersonCharacter::TryInteract()
{
	if (bAwaitingHoldInteractResult)
	{
		return;
	}

	if (!CanInteract())
	{
		UE_LOG(LogTemp, Warning, TEXT("%s %s TryInteract blocked"), OutlierNet::GetNetPrefix(this), *GetName());
		return;
	}

	if (!GetController())
	{
		UE_LOG(LogTemp, Warning, TEXT("%s %s TryInteract blocked: controller is null"), OutlierNet::GetNetPrefix(this), *GetName());
		return;
	}

	FVector NodeAimDirection = FVector::ZeroVector;
	AActor* TargetActor = FindInteractTargetByTrace(&NodeAimDirection);
	if (!TargetActor)
	{
		OUTLIER_LOG(Core, LogTemp, Log, TEXT("%s %s TryInteract miss"), OutlierNet::GetNetPrefix(this), *GetName());

		CancelLocalHoldInteract(true);

		return;
	}

	if (IInteractableInterface* Interactable = Cast<IInteractableInterface>(TargetActor))
	{
		if (UInteractableComponent* InteractableComponent = Interactable->GetInteractableComponent();
			InteractableComponent && InteractableComponent->RequiresHoldInteract())
		{
			if (HoldingInteractActor == TargetActor)
			{
				return;
			}

			if (HoldingInteractActor && HoldingInteractActor != TargetActor)
			{
				CancelLocalHoldInteract(true);
			}

			AInteractionNode* Node = Cast<AInteractionNode>(TargetActor);
			const int32 HoldAttemptId = Node ? Node->BeginLocalHoldAttempt(this) : 0;
			if (Node && HoldAttemptId == 0)
			{
				return;
			}
			if (!InteractableComponent->BeginHoldInteract(this, HoldAttemptId))
			{
				if (Node)
				{
					Node->EndLocalHoldAttempt(this, true);
				}
				return;
			}

			HoldingInteractActor = TargetActor;
			ServerBeginHoldInteract(TargetActor, HoldAttemptId, NodeAimDirection);
			return;
		}
	}

	//Hold Interaction Actor의 판정은 Hold Actor가 ServerInteract를 실행하는 식으로 처리. 업데이트는 해당 Actor에서 처리. 판정은 Character 에서
	//Hold or 즉발
	ServerInteract(TargetActor, 0, NodeAimDirection);
}

void AFirstPersonCharacter::EndInteract()
{
	if (!HoldingInteractActor || bAwaitingHoldInteractResult)
	{
		return;
	}

	CancelLocalHoldInteract(true);
}

void AFirstPersonCharacter::NotifyHoldInteractCompleted(AActor* CompletedActor)
{
	if (!CompletedActor || CompletedActor != HoldingInteractActor)
	{
		return;
	}

	const AInteractionNode* Node = Cast<AInteractionNode>(CompletedActor);
	// 노드는 시작 승인 대상을 유지한다. 완료 시 커서 위치가 바뀌어도 새 대상을 선택하지 않는다.
	if (Node ? !CanMaintainNodeInteraction(Node) : FindInteractTargetByTrace() != HoldingInteractActor)
	{
		CancelLocalHoldInteract(true);
		return;
	}

	bAwaitingHoldInteractResult = true;
	ServerInteract(CompletedActor, Node ? Node->GetLocalHoldAttemptId(this) : 0);
}

void AFirstPersonCharacter::CancelLocalHoldInteract(bool bNotifyServer)
{
	AActor* CanceledActor = HoldingInteractActor.Get();
	AInteractionNode* Node = Cast<AInteractionNode>(CanceledActor);
	// 로컬 시도를 정리하기 전에 취소 RPC에 보낼 번호를 확보한다.
	const int32 HoldAttemptId = Node ? Node->GetLocalHoldAttemptId(this) : 0;
	if (!CanceledActor)
	{
		bAwaitingHoldInteractResult = false;
		return;
	}

	if (IInteractableInterface* Interactable = Cast<IInteractableInterface>(CanceledActor))
	{
		if (UInteractableComponent* InteractableComponent = Interactable->GetInteractableComponent())
		{
			InteractableComponent->EndHoldInteract(this, true);
		}
	}
	if (Node)
	{
		Node->EndLocalHoldAttempt(this, true);
	}

	if (FocusedInteractable == CanceledActor)
	{
		FocusedInteractable = nullptr;
	}

	HoldingInteractActor = nullptr;
	bAwaitingHoldInteractResult = false;

	if (bNotifyServer)
	{
		ServerCancelHoldInteract(CanceledActor, HoldAttemptId);
	}
}

void AFirstPersonCharacter::NotifyHoldInteractInvalidated(AActor* TargetActor)
{
	if (HasAuthority())
	{
		const AInteractionNode* Node = Cast<AInteractionNode>(TargetActor);
		ClientOnHoldInteractFailed(TargetActor, Node ? Node->GetServerHoldAttemptId(this) : 0);
	}
}

void AFirstPersonCharacter::ServerBeginHoldInteract_Implementation(AActor* TargetActor, int32 HoldAttemptId,
	FVector_NetQuantizeNormal NodeAimDirection)
{
	AInteractionNode* Node = Cast<AInteractionNode>(TargetActor);
	if (Node)
	{
		if (!Node->AcceptServerHoldAttempt(this, HoldAttemptId))
		{
			return;
		}
	}
	IInteractableInterface* Interactable = Cast<IInteractableInterface>(TargetActor);
	UInteractableComponent* InteractableComponent =
		Interactable ? Interactable->GetInteractableComponent() : nullptr;

	if (!TargetActor
		|| !CanInteract()
		|| !GetController()
		|| (Node
			? !ValidateNodeStart(Node, NodeAimDirection)
			: !IsInteractTargetByTrace(TargetActor))
		|| !InteractableComponent
		|| !InteractableComponent->RequiresHoldInteract()
		|| !InteractableComponent->BeginHoldInteract(this, HoldAttemptId))
	{
		if (InteractableComponent)
		{
			InteractableComponent->EndHoldInteract(this, true);
		}
		ClientOnHoldInteractFailed(TargetActor, HoldAttemptId);
	}
}

void AFirstPersonCharacter::ServerInteract_Implementation(AActor* TargetActor, int32 HoldAttemptId,
	FVector_NetQuantizeNormal NodeAimDirection)
{
	const AInteractionNode* Node = Cast<AInteractionNode>(TargetActor);
	const bool bNodeInteraction = Node != nullptr;
	if (Node && HoldAttemptId != 0 && !Node->IsServerHoldAttemptCurrent(this, HoldAttemptId))
	{
		return;
	}
	if (!TargetActor || !CanInteract())
	{
		UE_LOG(LogTemp, Warning, TEXT("[Server] %s ServerInteract blocked Target=%s"), *GetName(), *GetNameSafe(TargetActor));
		ServerCancelHoldInteract_Implementation(TargetActor, HoldAttemptId);
		ClientOnHoldInteractFailed(TargetActor, HoldAttemptId);
		return;
	}

	if (!GetController())
	{
		UE_LOG(LogTemp, Warning, TEXT("[Server] %s ServerInteract blocked: controller is null"), *GetName());
		ServerCancelHoldInteract_Implementation(TargetActor, HoldAttemptId);
		ClientOnHoldInteractFailed(TargetActor, HoldAttemptId);
		return;
	}

	// 준비 요청은 몸체 Ray를 검증하고, 홀드 완료는 기존 서버 세션과 유지 조건을 검증한다.
	const bool bValidTarget = Node
		? (HoldAttemptId != 0 ? CanMaintainNodeInteraction(Node)
			: ValidateNodeStart(Node, NodeAimDirection))
		: IsInteractTargetByTrace(TargetActor);
	if (!bValidTarget)
	{
		UE_LOG(LogTemp, Warning, TEXT("[Server] %s ServerInteract validation failed Requested=%s"), *GetName(), *GetNameSafe(TargetActor));
		ServerCancelHoldInteract_Implementation(TargetActor, HoldAttemptId);
		ClientOnHoldInteractFailed(TargetActor, HoldAttemptId);
		return;
	}

	if (IInteractableInterface* Interactable = Cast<IInteractableInterface>(TargetActor))
	{
		UInteractableComponent* InteractableComponent = Interactable->GetInteractableComponent();
		if (!InteractableComponent
			|| !InteractableComponent->CanInteract(GetOwnedGameplayTagsForQuery()))
		{
			ServerCancelHoldInteract_Implementation(TargetActor, HoldAttemptId);
			ClientOnHoldInteractFailed(TargetActor, HoldAttemptId);
			return;
		}

		const bool bHoldInteraction = InteractableComponent->RequiresHoldInteract();
		// 0은 준비 요청이다. 범위 이탈로 클라이언트만 준비 태그를 지웠다면
		// 이미 준비된 서버 상태를 다시 알려준다. Interact/보상/현재 홀드에는 손대지 않는다.
		if (bNodeInteraction && bHoldInteraction && HoldAttemptId == 0)
		{
			ClientOnInteractSucceeded(TargetActor, EInteractionFlowResult::HoldReady, 0);
			return;
		}
		// 실제 누르기 완료에는 유효한 시도 번호와 완료된 서버 세션이 필요하다.
		if (bNodeInteraction && bHoldInteraction != (HoldAttemptId != 0))
		{
			ClientOnHoldInteractFailed(TargetActor, HoldAttemptId);
			return;
		}
		if (bHoldInteraction && !InteractableComponent->CanCommitHoldInteraction(this))
		{
			ServerCancelHoldInteract_Implementation(TargetActor, HoldAttemptId);
			ClientOnHoldInteractFailed(TargetActor, HoldAttemptId);
			return;
		}

		if (!Interactable->Interact(this))
		{
			UE_LOG(LogTemp, Warning,
				TEXT("[Server] %s ServerInteract rejected by target Target=%s"),
				*GetName(),
				*GetNameSafe(TargetActor));
			ServerCancelHoldInteract_Implementation(TargetActor, HoldAttemptId);
			ClientOnHoldInteractFailed(TargetActor, HoldAttemptId);
			return;
		}

		// true는 비동기 대상의 예약 수락일 수 있다. 성공 UI/이벤트는 실제 commit 경로에서 보낸다.
		if (Interactable->DefersInteractionCompletion())
		{
			return;
		}
		const EInteractionFlowResult FlowResult =
			!bHoldInteraction && InteractableComponent->RequiresHoldInteract()
				? EInteractionFlowResult::HoldReady
				: EInteractionFlowResult::Completed;

		if (bHoldInteraction)
		{
			InteractableComponent->CommitHoldInteraction(this);
		}
		else if (FlowResult == EInteractionFlowResult::Completed)
		{
			InteractableComponent->CommitInteraction(this);
		}

		OUTLIER_LOG(Core, LogTemp, Log, TEXT("[Server] %s ServerInteract success Target=%s"), *GetName(), *GetNameSafe(TargetActor));
		ClientOnInteractSucceeded(TargetActor, FlowResult, HoldAttemptId); //InteractObject UI Update;
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("[Server] %s ServerInteract failed: target not interactable Target=%s"), *GetName(), *GetNameSafe(TargetActor));
		ServerCancelHoldInteract_Implementation(TargetActor, HoldAttemptId);
		ClientOnHoldInteractFailed(TargetActor, HoldAttemptId);
	}
}

void AFirstPersonCharacter::ServerCancelHoldInteract_Implementation(AActor* TargetActor, int32 HoldAttemptId)
{
	const AInteractionNode* Node = Cast<AInteractionNode>(TargetActor);
	if (Node && HoldAttemptId == 0)
	{
		// 준비 상태는 아직 시도 번호가 없다. 실제 홀드가 없을 때만 서버 준비 상태도 정리한다.
		// 늦게 온 준비 취소가 이후 시작된 홀드(다른 플레이어 포함)를 끊어서는 안 된다.
		UInteractableComponent* Component = Node->GetInteractableComponent();
		if (Component) Component->ResetHoldReadyIfIdle();
		return;
	}
	if (Node && !Node->IsServerHoldAttemptCurrent(this, HoldAttemptId)) return;
	if (IInteractableInterface* Interactable = Cast<IInteractableInterface>(TargetActor))
	{
		if (UInteractableComponent* InteractableComponent = Interactable->GetInteractableComponent())
		{
			InteractableComponent->ResetHoldInteraction(this);
		}
	}
}

void AFirstPersonCharacter::ClientOnInteractSucceeded_Implementation(
	AActor* TargetActor,
	EInteractionFlowResult FlowResult,
	int32 HoldAttemptId)
{
	AInteractionNode* Node = Cast<AInteractionNode>(TargetActor);
	// 번호는 노드별로 같을 수 있다. 대상과 번호를 함께 확인하며 늦은 HoldReady(0)도 거절한다.
	if (Node && ((HoldingInteractActor && HoldingInteractActor != TargetActor)
		|| HoldAttemptId != Node->GetLocalHoldAttemptId(this))) return;
	const bool bCompletedHoldInteract =
		bAwaitingHoldInteractResult &&
		HoldingInteractActor == TargetActor;

	if (bCompletedHoldInteract)
	{
		if (IInteractableInterface* Interactable = Cast<IInteractableInterface>(TargetActor))
		{
			if (UInteractableComponent* InteractableComponent = Interactable->GetInteractableComponent())
			{
				InteractableComponent->EndHoldInteract(this, false);
			}
		}

		HoldingInteractActor = nullptr;
		if (Node) Node->EndLocalHoldAttempt(this, false);
		bAwaitingHoldInteractResult = false;
	}

	FocusedInteractable = TargetActor;

	if (IInteractableInterface* Interactable = Cast<IInteractableInterface>(TargetActor))
	{
		if (UInteractableComponent* InteractableComponent = Interactable->GetInteractableComponent())
		{
			InteractableComponent->HandleInteractionSucceeded(
				this,
				FlowResult,
				!HasAuthority());
		}
	}
}

void AFirstPersonCharacter::ClientOnHoldInteractFailed_Implementation(AActor* TargetActor, int32 HoldAttemptId)
{
	if (TargetActor && HoldingInteractActor != TargetActor)
	{
		return;
	}
	const AInteractionNode* Node = Cast<AInteractionNode>(HoldingInteractActor.Get());
	if (HoldAttemptId != (Node ? Node->GetLocalHoldAttemptId(this) : 0)) return;

	CancelLocalHoldInteract(false);
}

void AFirstPersonCharacter::ClientShowDamageFeedback_Implementation(
	FVector_NetQuantize DamageOrigin,
	float HealthDamage,
	float ShieldDamage)
{
	if (!IsLocallyControlled())
	{
		return;
	}

	APlayerController* PlayerController = Cast<APlayerController>(GetController());
	ULocalPlayer* LocalPlayer = PlayerController ? PlayerController->GetLocalPlayer() : nullptr;
	if (!LocalPlayer)
	{
		return;
	}

	if (ULocalPlayerUISubSystem* UISubSystem = LocalPlayer->GetSubsystem<ULocalPlayerUISubSystem>())
	{
		UISubSystem->OnDamageFeedback(this, FVector(DamageOrigin), HealthDamage, ShieldDamage);
	}
}

FGameplayTagContainer AFirstPersonCharacter::GetOwnedGameplayTagsForQuery() const
{
	return OwnedQueryTags;
}

FGameplayTag AFirstPersonCharacter::GetCurrentRoomTag() const
{
	return RoomTagComponent ? RoomTagComponent->GetCurrentRoomTag() : FGameplayTag();
}

FGameplayTag AFirstPersonCharacter::GetDefaultRoomTag() const
{
	return RoomTagComponent ? RoomTagComponent->GetDefaultRoomTag() : FGameplayTag();
}

void AFirstPersonCharacter::OnRep_CurrentWeapon()
{
	OUTLIER_LOG(Core, LogTemp, Log, TEXT("%s %s OnRep_CurrentWeapon Previous=%s Current=%s"), OutlierNet::GetNetPrefix(this), *GetName(), *GetNameSafe(LastReplicatedWeapon), *GetNameSafe(CurrentWeapon));
	CurrentWeaponType = CurrentWeapon ? CurrentWeapon->GetWeaponType() : EWeaponType::Unarmed;
	LastReplicatedWeapon = CurrentWeapon;
	OnWeaponChanged.Broadcast(CurrentWeaponType);
	if (UOutlierStencilSubsystem* Outlines = GetWorld()->GetSubsystem<UOutlierStencilSubsystem>())
	{
		Outlines->MarkTeammatesDirty();
	}
}

void AFirstPersonCharacter::TryStartAttack()
{
	OUTLIER_LOG(Core, LogTemp, Log, TEXT("FirstPerson"));
	if (!CurrentWeapon)
	{
		UE_LOG(LogTemp, Warning, TEXT("%s %s TryStartAttack blocked: no weapon equipped"), OutlierNet::GetNetPrefix(this), *GetName());
		return;
	}

	OUTLIER_LOG(Core, LogTemp, Log, TEXT("%s %s TryStartAttack Weapon=%s"), OutlierNet::GetNetPrefix(this), *GetName(), *GetNameSafe(CurrentWeapon));
	CurrentWeapon->StartAttack();
}

void AFirstPersonCharacter::TryStopAttack()
{
	if (!CurrentWeapon)
	{
		UE_LOG(LogTemp, Warning, TEXT("%s %s TryStopAttack blocked: no weapon equipped"), OutlierNet::GetNetPrefix(this), *GetName());
		return;
	}

	OUTLIER_LOG(Core, LogTemp, Log, TEXT("%s %s TryStopAttack Weapon=%s"), OutlierNet::GetNetPrefix(this), *GetName(), *GetNameSafe(CurrentWeapon));
	CurrentWeapon->StopAttack();
}

void AFirstPersonCharacter::EquipWeapon(AWeaponBase* Weapon)
{
	OutlierFirstPickup::FScope Scope(TEXT("EquipLifecycleAndDelegates"), Weapon);
	if (CurrentWeapon == Weapon)
	{
		OUTLIER_LOG(Core,
			LogTemp,
			Log,
			TEXT("%s %s EquipWeapon skipped: already equipped %s"),
			OutlierNet::GetNetPrefix(this),
			*GetName(),
			*GetNameSafe(Weapon)
		);
		return;
	}

	OUTLIER_LOG(Core,
		LogTemp,
		Log,
		TEXT("%s %s EquipWeapon Previous=%s New=%s"),
		OutlierNet::GetNetPrefix(this),
		*GetName(),
		*GetNameSafe(CurrentWeapon),
		*GetNameSafe(Weapon)
	);

	if (CurrentWeapon && CurrentWeapon->GetOwner() == this)
	{
		CurrentWeapon->OnUnequipped();
	}

	CurrentWeapon = Weapon;
	CurrentWeaponType = CurrentWeapon ? CurrentWeapon->GetWeaponType() : EWeaponType::Unarmed;

	if (CurrentWeapon)
	{
		CurrentWeapon->OnEquipped(this);
		OnWeaponChanged.Broadcast(CurrentWeapon->GetWeaponType());
	}

	LastReplicatedWeapon = CurrentWeapon;
	ForceNetUpdate();
	// nullptr 장착에는 OnWeaponChanged가 없으므로 해제도 같은 경로에서 캐시를 무효화한다.
	if (GetNetMode() != NM_DedicatedServer)
	{
		if (UOutlierStencilSubsystem* Outlines = GetWorld()->GetSubsystem<UOutlierStencilSubsystem>())
		{
			Outlines->MarkTeammatesDirty();
		}
	}

	OUTLIER_LOG(Core, LogTemp, Log, TEXT("%s %s EquipWeapon complete Current=%s"), OutlierNet::GetNetPrefix(this), *GetName(), *GetNameSafe(CurrentWeapon));

	
}

void AFirstPersonCharacter::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AFirstPersonCharacter, CurrentWeapon);
	DOREPLIFETIME(AFirstPersonCharacter, SuitTransitionBlockId);
}

void AFirstPersonCharacter::CompleteDeferredInteraction(AActor* TargetActor, bool bSucceeded)
{
	// 비동기 대상은 ServerInteract의 즉시 성공 통지를 건너뛴다. 실제 commit/실패가 확정된 경로에서만 호출한다.
	if (!HasAuthority() || !IsValid(TargetActor))
	{
		return;
	}
	const AInteractionNode* Node = Cast<AInteractionNode>(TargetActor);
	const int32 HoldAttemptId = Node ? Node->GetServerHoldAttemptId(this) : 0;
	if (!bSucceeded)
	{
		ServerCancelHoldInteract_Implementation(TargetActor, HoldAttemptId);
		ClientOnHoldInteractFailed(TargetActor, HoldAttemptId);
		return;
	}
	if (IInteractableInterface* Interactable = Cast<IInteractableInterface>(TargetActor))
	{
		if (UInteractableComponent* Component = Interactable->GetInteractableComponent())
		{
			if (Component->RequiresHoldInteract())
			{
				Component->CommitHoldInteraction(this);
			}
			else
			{
				Component->CommitInteraction(this);
			}
			ClientOnInteractSucceeded(TargetActor, EInteractionFlowResult::Completed, HoldAttemptId);
		}
	}
}

bool AFirstPersonCharacter::AcquirePresentationProtection(UObject* ProtectionOwner, const FGuid& ExecutionId)
{
	if (!HasAuthority() || !IsValid(ProtectionOwner) || !ExecutionId.IsValid() || IsActorBeingDestroyed()) return false;
	AActor* ActorOwner = Cast<AActor>(ProtectionOwner);
	if (!ActorOwner || ActorOwner->GetWorld() != GetWorld() || ActorOwner->IsActorBeingDestroyed()) return false;
	const uint32 Generation = PresentationArena.IsValid() ? PresentationArena->GetGameplayGeneration() : 0;
	if (const FPresentationProtectionOwner* Existing = PresentationProtectionOwners.Find(ExecutionId))
	{
		return Existing->Owner.Get() == ProtectionOwner && Existing->Generation == Generation;
	}
	const bool bWasProtected = IsPresentationProtected();
	PresentationProtectionOwners.Add(ExecutionId, {ProtectionOwner, Generation});
	ActorOwner->OnEndPlay.AddUniqueDynamic(this, &ThisClass::HandlePresentationOwnerEndPlay);
	// 상태를 먼저 게시한다. AI 정리 중 재진입하더라도 같은 대상을 재획득하지 못한다.
	if (!bWasProtected && PresentationEnemyRooms.IsValid())
	{
		PresentationEnemyRooms->SetPresentationTargetProtected(this, true);
	}
	OUTLIER_LOG(Core, LogTemp, Log, TEXT("[PresentationProtection] Acquire Pawn=%s Owner=%s Execution=%s Generation=%u Count=%d"),
		*GetName(), *GetNameSafe(ProtectionOwner), *ExecutionId.ToString(), Generation, PresentationProtectionOwners.Num());
	// 타깃 정리 delegate가 연출을 취소할 수도 있으므로 호출자에게 현재 소유권을 다시 확인해 준다.
	const FPresentationProtectionOwner* Acquired = PresentationProtectionOwners.Find(ExecutionId);
	return Acquired && Acquired->Owner.Get() == ProtectionOwner && Acquired->Generation == Generation;
}

void AFirstPersonCharacter::ReleasePresentationProtection(UObject* ProtectionOwner, const FGuid& ExecutionId)
{
	if (!HasAuthority()) return;
	const FPresentationProtectionOwner* Existing = PresentationProtectionOwners.Find(ExecutionId);
	if (!Existing || Existing->Owner.Get() != ProtectionOwner) return;
	const uint32 Generation = Existing->Generation;
	PresentationProtectionOwners.Remove(ExecutionId);
	OUTLIER_LOG(Core, LogTemp, Log, TEXT("[PresentationProtection] Release Pawn=%s Owner=%s Execution=%s Generation=%u Count=%d"),
		*GetName(), *GetNameSafe(ProtectionOwner), *ExecutionId.ToString(), Generation, PresentationProtectionOwners.Num());
	// 한 소유자가 여러 연출을 진행할 수 있으므로 마지막 실행을 놓을 때만 구독을 푼다.
	bool bOwnerStillActive = false;
	for (const auto& Entry : PresentationProtectionOwners)
	{
		if (Entry.Value.Owner.Get() == ProtectionOwner) { bOwnerStillActive = true; break; }
	}
	if (!bOwnerStillActive)
	{
		if (AActor* ActorOwner = Cast<AActor>(ProtectionOwner))
			ActorOwner->OnEndPlay.RemoveDynamic(this, &ThisClass::HandlePresentationOwnerEndPlay);
	}
	if (!IsPresentationProtected() && PresentationEnemyRooms.IsValid())
	{
		PresentationEnemyRooms->SetPresentationTargetProtected(this, false);
	}
}

void AFirstPersonCharacter::HandlePresentationOwnerEndPlay(AActor* ProtectionOwner, EEndPlayReason::Type Reason)
{
	TArray<FGuid> ReleasedIds;
	for (const auto& Entry : PresentationProtectionOwners)
	{
		if (Entry.Value.Owner.Get() == ProtectionOwner) ReleasedIds.Add(Entry.Key);
	}
	for (const FGuid& Id : ReleasedIds) ReleasePresentationProtection(ProtectionOwner, Id);
}

void AFirstPersonCharacter::ClearPresentationProtection(uint32 Generation)
{
	const bool bWasProtected = IsPresentationProtected();
	if (bWasProtected)
		OUTLIER_LOG(Core, LogTemp, Log, TEXT("[PresentationProtection] Clear Pawn=%s Generation=%u Count=%d"),
			*GetName(), Generation, PresentationProtectionOwners.Num());
	for (const auto& Entry : PresentationProtectionOwners)
	{
		if (AActor* ProtectionOwner = Cast<AActor>(Entry.Value.Owner.Get()))
			ProtectionOwner->OnEndPlay.RemoveDynamic(this, &ThisClass::HandlePresentationOwnerEndPlay);
	}
	PresentationProtectionOwners.Reset();
	if (bWasProtected && PresentationEnemyRooms.IsValid() && !IsActorBeingDestroyed())
		PresentationEnemyRooms->SetPresentationTargetProtected(this, false);
}

bool AFirstPersonCharacter::AcquireSuitTransitionBlock(const FGuid& TransitionId)
{
	if (!HasAuthority() || !TransitionId.IsValid() || IsSuitTransitionBlocked())
	{
		return false;
	}
	SuitTransitionBlockId = TransitionId;
	// 서버도 클라이언트 RepNotify와 같은 취소/이동 정지 경로를 즉시 실행한다.
	OnRep_SuitTransitionBlockId();
	ForceNetUpdate();
	return true;
}

void AFirstPersonCharacter::ReleaseSuitTransitionBlock(const FGuid& TransitionId)
{
	// 차단을 획득한 전환 ID만 해제할 수 있다. 이전 전환의 늦은 종료 요청이 새 전환을 풀지 못하게 한다.
	if (!HasAuthority() || !TransitionId.IsValid() || SuitTransitionBlockId != TransitionId)
	{
		return;
	}
	SuitTransitionBlockId.Invalidate();
	OnRep_SuitTransitionBlockId();
	ForceNetUpdate();
}

bool AFirstPersonCharacter::TeleportTo(const FVector& DestLocation, const FRotator& DestRotation,
	bool bIsATest, bool bNoCheck)
{
	const bool bSucceeded = Super::TeleportTo(DestLocation, DestRotation, bIsATest, bNoCheck);
	if (!bIsATest && !IsActorBeingDestroyed() && IsSuitTransitionBlocked())
	{
		// 엔진의 순간이동 후 바닥 판정이 None을 Falling으로 바꿀 수 있다.
		// 성공/실패 후 현재 차단만 재적용하고, 최초 복원 모드와 소유권은 건드리지 않는다.
		ResetMovementInput();
		GetCharacterMovement()->StopMovementImmediately();
		GetCharacterMovement()->DisableMovement();
	}
	return bSucceeded;
}

void AFirstPersonCharacter::OnRep_SuitTransitionBlockId()
{
	UCharacterMovementComponent* Movement = GetCharacterMovement();
	ResetMovementInput();
	if (IsSuitTransitionBlocked())
	{
		// Slide/비행 등 기존 행동 취소가 이동 모드를 바꿀 수 있어, 취소 후의 복구 가능한 모드를 기록한다.
		PrepareForSuitTransition();
		StopJumping();
		ConsumeMovementInputVector();
		if (!bSuitTransitionOwnsMovementStop)
		{
			// 시작부터 MOVE_None이었다면 이 전환이 멈춘 것이 아니므로 종료 시 이동 모드를 복구하지 않는다.
			MovementModeBeforeSuitTransition = Movement->MovementMode;
			CustomMovementModeBeforeSuitTransition = Movement->CustomMovementMode;
			bSuitTransitionOwnsMovementStop = Movement->MovementMode != MOVE_None;
		}
		Movement->StopMovementImmediately();
		Movement->DisableMovement();
	}
	else
	{
		// 다른 원인의 이동 정지/사망/Reboot는 해제하지 않는다. 이 전환이 멈춘 살아 있는 Pawn만 복구한다.
		// 원래 비행하던 Partner에 한해 순간이동으로 남은 Falling도 복구한다.
		// Shooter의 정상 낙하나 다른 시스템이 적용한 모드는 이 예외에 포함하지 않는다.
		const bool bRestoreTeleportedFlight = CanRestoreFlyingAfterSuitTransition()
			&& MovementModeBeforeSuitTransition == MOVE_Flying && Movement->MovementMode == MOVE_Falling;
		if (bSuitTransitionOwnsMovementStop && !IsActorBeingDestroyed()
			&& (Movement->MovementMode == MOVE_None || bRestoreTeleportedFlight)
			&& CanResumeMovementAfterSuitTransition())
		{
			Movement->SetMovementMode(MovementModeBeforeSuitTransition, CustomMovementModeBeforeSuitTransition);
		}
		bSuitTransitionOwnsMovementStop = false;
	}
	OnSuitTransitionBlockChanged();
}

void AFirstPersonCharacter::PrepareForSuitTransition()
{
	if (HasAuthority() && CurrentWeapon)
	{
		CurrentWeapon->StopAttack();
	}
}

void AFirstPersonCharacter::UnPossessed()
{
	ClearPresentationProtection(0);
	ClearInteractionDetection();
	OnSuitTransitionParticipantInvalidated.Broadcast(this);
	Super::UnPossessed();
}

EWeaponType AFirstPersonCharacter::GetWeaponType() const
{
	return CurrentWeaponType;
}

void AFirstPersonCharacter::CollectStealthMeshes(
	TArray<UMeshComponent*>& OutFirstPersonMeshes,
	TArray<UMeshComponent*>& OutThirdPersonMeshes) const
{
	// 은신 머티리얼/스텐실 적용과 원상복구는 UMaterialPostProcessSubsystem 이 전담한다.
	// 여기서는 "내 메시가 어떻게 구성돼 있는지"와 지금 든 무기만 알려준다.
	if (FirstPersonMesh)
	{
		OutFirstPersonMeshes.Add(FirstPersonMesh);
	}

	if (USkeletalMeshComponent* ThirdPersonMesh = GetMesh())
	{
		OutThirdPersonMeshes.Add(ThirdPersonMesh);
	}

	if (const AWeaponBase* EquippedWeapon = GetCurrentWeapon())
	{
		EquippedWeapon->CollectStealthMeshes(OutFirstPersonMeshes, OutThirdPersonMeshes);
	}
}

void AFirstPersonCharacter::OnMoveInputUpdated(const FVector2D& MoveValue)
{
}

void AFirstPersonCharacter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	// 몸체 호버는 입력에 매 프레임 반응한다. 주변 범위 탐색은 기존 타이머에서만 수행한다.
	UpdateNodeInteractionHover();
}

void AFirstPersonCharacter::UpdateInteractableFocus()
{
	InteractionNodes.RemoveAllSwap([](const TWeakObjectPtr<AInteractionNode>& Node) { return !Node.IsValid(); });
	const AFirstPersonPlayerController* PlayerController = Cast<AFirstPersonPlayerController>(GetController());
	if (PlayerController && PlayerController->IsHudHiddenForDeath())
	{
		// GameOver dismiss 이후 탐색 타이머가 안내 UI를 다시 켜지 않도록 한다.
		// 표시와 범위 캐시만 정리하고, 상호작용 취소는 기존 수명 주기에 맡긴다.
		ClearInteractionDetection();
		return;
	}

	NormalizeInteractionSphereRanges();
	// Inner owns overlapping actors. Refresh its cache before querying the outer sphere.
	TArray<AActor*> CurrentInteractables;
	if (IsLocallyControlled())
	{
		GetInteractablesInRange(CurrentInteractables);
	}
	SyncInteractableKeyWidgets(CurrentInteractables);

	TArray<AActor*> CurrentOutlineInteractables;
	if (IsLocallyControlled())
	{
		GetInteractablesInSphere(OutlineSphereRange, true, CurrentOutlineInteractables);
	}
	SyncOutlineInteractables(CurrentOutlineInteractables);
}

void AFirstPersonCharacter::UpdateNodeInteractionHover()
{
	if (!IsLocallyControlled())
	{
		return;
	}
	AInteractionNode* NextHovered = CanUseNodeInteraction() && !HoldingInteractActor
		? Cast<AInteractionNode>(FindInteractTargetByTrace()) : nullptr;
	if (HoveredInteractionNode.Get() != NextHovered)
	{
		if (AInteractionNode* Previous = HoveredInteractionNode.Get())
		{
			Previous->GetInteractableComponent()->InteractInfoWidgetDeactivate();
		}
		HoveredInteractionNode = NextHovered;
		if (NextHovered)
		{
			NextHovered->GetInteractableComponent()->InteractInfoWidgetActivate(this);
		}
	}
}

void AFirstPersonCharacter::SyncOutlineInteractables(const TArray<AActor*>& CurrentInteractables)
{
	for (AActor* PreviousActor : OutlineInteractables)
	{
		// Inner has already taken ownership; do not clear the state it just applied.
		if (IsValid(PreviousActor)
			&& !CurrentInteractables.Contains(PreviousActor)
			&& !NearbyInteractables.Contains(PreviousActor))
		{
			if (IInteractableInterface* Interactable = Cast<IInteractableInterface>(PreviousActor))
			{
				if (UInteractableComponent* Component = Interactable->GetInteractableComponent())
				{
					Component->InteractionOutlineWidgetDeactivate();
					Component->OnOutlineRangeChanged(this, false);
				}
			}
		}
	}

	for (AActor* CurrentActor : CurrentInteractables)
	{
		if (IsValid(CurrentActor))
		{
			if (IInteractableInterface* Interactable = Cast<IInteractableInterface>(CurrentActor))
			{
				if (UInteractableComponent* Component = Interactable->GetInteractableComponent())
				{
					Component->InteractionOutlineWidgetActivate(this);
					if (!OutlineInteractables.Contains(CurrentActor))
					{
						Component->OnOutlineRangeChanged(this, true);
					}
				}
			}
		}
	}

	OutlineInteractables.Reset(CurrentInteractables.Num());
	for (AActor* CurrentActor : CurrentInteractables)
	{
		if (IsValid(CurrentActor))
		{
			OutlineInteractables.Add(CurrentActor);
		}
	}
}

void AFirstPersonCharacter::GetInteractablesInRange(TArray<AActor*>& OutInteractables) const
{
	GetInteractablesInSphere(InteractionWidgetExposedRange, false, OutInteractables);
}

void AFirstPersonCharacter::ClearInteractionDetection()
{
	if (AInteractionNode* Hovered = HoveredInteractionNode.Get())
	{
		Hovered->GetInteractableComponent()->InteractInfoWidgetDeactivate();
	}
	HoveredInteractionNode.Reset();
	for (AActor* Actor : NearbyInteractables)
	{
		if (IsValid(Actor))
		{
			if (IInteractableInterface* Interactable = Cast<IInteractableInterface>(Actor))
			{
				if (UInteractableComponent* Component = Interactable->GetInteractableComponent())
				{
					Component->InteractKeyWidgetDeactivate();
					Component->InteractionLockedWidgetDeactivate();
					Component->InteractionOutlineWidgetDeactivate();
					Component->InteractInfoWidgetDeactivate();
				}
			}
		}
	}
	NearbyInteractables.Reset();
	SyncOutlineInteractables(TArray<AActor*>());
}

void AFirstPersonCharacter::NormalizeInteractionSphereRanges()
{
	InteractionWidgetExposedRange = FMath::Max(InteractionWidgetExposedRange, 0.0f);
	OutlineSphereRange = FMath::Max(OutlineSphereRange, InteractionWidgetExposedRange + 1.0f);
}

#if WITH_EDITOR
void AFirstPersonCharacter::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	NormalizeInteractionSphereRanges();
	Super::PostEditChangeProperty(PropertyChangedEvent);
}
#endif

void AFirstPersonCharacter::GetInteractablesInSphere(
	float Range, bool bOutlineSphere, TArray<AActor*>& OutInteractables) const
{
	if (!GetWorld() || !GetController())
	{
		return;
	}

	const FVector SphereCenter = FirstPersonCamera
		? FirstPersonCamera->GetComponentLocation()
		: GetActorLocation();

	// 일반 Overlap과 독립된 캐시를 사용하므로 NoCollision 몸체도 안내/수집 후보가 된다.
	for (const TWeakObjectPtr<AInteractionNode>& Entry : InteractionNodes)
	{
		AInteractionNode* Node = Entry.Get();
		if (!Node || Node->IsHidden() || (bOutlineSphere && NearbyInteractables.Contains(Node)))
		{
			continue;
		}
		UInteractableComponent* Component = Node->GetInteractableComponent();
		if (!(bOutlineSphere ? Component->CanShowInteractionOutlineWidget()
			: Component->CanShowInteractionWidgets(GetOwnedGameplayTagsForQuery())))
		{
			continue;
		}
		for (const TWeakObjectPtr<UPrimitiveComponent>& Body : Node->GetInteractionBodies())
		{
			if (Node->IsInteractionBodyVisible(Body.Get())
				&& Body->Bounds.GetBox().ComputeSquaredDistanceToPoint(SphereCenter) <= FMath::Square(Range)
				&& HasInteractionLineOfSight(Node, Body.Get()))
			{
				OutInteractables.AddUnique(Node);
				break;
			}
		}
	}

	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(InteractionSphereOverlap), false, this);
	QueryParams.AddIgnoredActor(this);
	QueryParams.AddIgnoredActor(CurrentWeapon);

	TArray<FOverlapResult> Overlaps;
	GetWorld()->OverlapMultiByChannel(
		Overlaps,
		SphereCenter,
		FQuat::Identity,
		InteractionTraceChannel,
		FCollisionShape::MakeSphere(Range),
		QueryParams
	);

	const bool bDrawSphere = bDrawInteractionTrace || (bOutlineSphere && bDrawOutlineSphere);
	if (bDrawSphere)
	{
		const FColor TraceColor = !Overlaps.IsEmpty()
			? (bOutlineSphere ? FColor::Blue : FColor::Green)
			: FColor::Red;
		DrawDebugSphere(GetWorld(), SphereCenter, Range, 32, TraceColor, false, InteractionTraceInterval, 0, 1.5f);
		DrawDebugPoint(GetWorld(), SphereCenter, 12.0f, FColor::Yellow, false, InteractionTraceInterval, 0);
		DrawDebugString(GetWorld(), SphereCenter + FVector(0.0f, 0.0f, bOutlineSphere ? 32.0f : 16.0f),
			FString::Printf(TEXT("%s SphereOverlap | Radius %.0f cm"),
				bOutlineSphere ? TEXT("Outer Outline") : TEXT("Inner Interaction"), Range),
			nullptr, TraceColor, InteractionTraceInterval, true);
	}

	// OverlapMultiByChannel can return false with overlap-only hits; inspect the results.
	if (Overlaps.IsEmpty())
	{
		return;
	}

	TSet<AActor*> VisitedActors;
	const FGameplayTagContainer OwnedGameplayTags = GetOwnedGameplayTagsForQuery();

	for (const FOverlapResult& Overlap : Overlaps)
	{
		AActor* HitActor = Overlap.GetActor();
		if (!IsValid(HitActor) || HitActor->IsA<AInteractionNode>() || VisitedActors.Contains(HitActor)
			|| (bOutlineSphere && NearbyInteractables.Contains(HitActor)))
		{
			continue;
		}

		IInteractableInterface* Interactable = Cast<IInteractableInterface>(HitActor);
		if (!Interactable)
		{
			continue;
		}

		UInteractableComponent* InteractableComponent = Interactable->GetInteractableComponent();
		// Outer markers ignore player access; inner prompts still require interaction permissions.
		if (!IsValid(InteractableComponent)
			|| !(bOutlineSphere
				? InteractableComponent->CanShowInteractionOutlineWidget()
				: InteractableComponent->CanShowInteractionWidgets(OwnedGameplayTags)))
		{
			continue;
		}

		if (!HasInteractionLineOfSight(HitActor, Overlap.GetComponent()))
		{
			if (bOutlineSphere && bDrawSphere)
			{
				const FVector TargetLocation = Overlap.GetComponent()
					? Overlap.GetComponent()->Bounds.Origin : HitActor->GetActorLocation();
				DrawDebugLine(GetWorld(), SphereCenter, TargetLocation, FColor::Red,
					false, InteractionTraceInterval, 0, 1.0f);
				DrawDebugString(GetWorld(), TargetLocation, TEXT("Outer: Occluded"),
					nullptr, FColor::Red, InteractionTraceInterval, true);
			}
			continue;
		}

		if (bOutlineSphere && bDrawSphere)
		{
			const bool bLocked = !InteractableComponent->CanInteract(OwnedGameplayTags)
				&& InteractableComponent->IsLockedYet();
			const FColor TargetColor = bLocked ? FColor::Yellow : FColor::Cyan;
			const FVector TargetLocation = Overlap.GetComponent()
				? Overlap.GetComponent()->Bounds.Origin : HitActor->GetActorLocation();
			DrawDebugLine(GetWorld(), SphereCenter, TargetLocation, TargetColor,
				false, InteractionTraceInterval, 0, 1.0f);
			DrawDebugString(GetWorld(), TargetLocation,
				FString::Printf(TEXT("Outer: %s | %s"), *HitActor->GetName(),
					bLocked ? TEXT("Locked") : TEXT("Available")),
				nullptr, TargetColor, InteractionTraceInterval, true);
		}

		// 한 액터의 가려진 컴포넌트만 먼저 겹쳐도 다른 노출된 컴포넌트를 검사할 수 있게 한다.
		VisitedActors.Add(HitActor);
		OutInteractables.Add(HitActor);
	}
}

bool AFirstPersonCharacter::HasInteractionLineOfSight(
	const AActor* TargetActor, const UPrimitiveComponent* TargetComponent) const
{
	if (!IsValid(TargetActor) || !GetWorld() || !GetController())
	{
		return false;
	}

	FVector ViewLocation;
	FRotator ViewRotation;
	GetController()->GetPlayerViewPoint(ViewLocation, ViewRotation);
	// 액터 원점이 바닥 등에 놓인 대상도 실제 검색된 충돌 컴포넌트 위치로 검사한다.
	const FVector TargetLocation = TargetComponent
		? TargetComponent->Bounds.Origin
		: TargetActor->GetActorLocation();
	FCollisionQueryParams Params(SCENE_QUERY_STAT(InteractionLineOfSight), false, this);
	Params.AddIgnoredActor(this);
	Params.AddIgnoredActor(CurrentWeapon);
	// 대상의 큰 탐지 콜리전이 벽 앞으로 튀어나와 있어도 중간의 벽을 검사한다.
	Params.AddIgnoredActor(TargetActor);
	FHitResult Hit;
	return !GetWorld()->LineTraceSingleByChannel(
		Hit, ViewLocation, TargetLocation, InteractionVisibilityTraceChannel, Params);
}

void AFirstPersonCharacter::CacheInteractionNode(AActor* Actor)
{
	if (AInteractionNode* Node = Cast<AInteractionNode>(Actor))
	{
		InteractionNodes.AddUnique(Node);
	}
}

bool AFirstPersonCharacter::CanUseNodeInteraction() const
{
	if (!GetController() || !CanInteract()
		|| !GetOwnedGameplayTagsForQuery().HasTagExact(OutlierGameplayTags::Actor::Role::Partner()))
	{
		return false;
	}
	if (const AFirstPersonPlayerController* PC = Cast<AFirstPersonPlayerController>(GetController()))
	{
		if (PC->IsHudHiddenForDeath() || PC->IsFirstPersonInputMode(FirstPersonInputModeTags::UI()))
		{
			return false;
		}
	}
	if (const APartnerCharacter* Partner = Cast<APartnerCharacter>(this))
	{
		return Partner->CanAcceptInput()
			&& !(NodeBlockingHack.IsValid() && NodeBlockingHack->IsHackInteractionActive())
			&& !(NodeBlockingEMP.IsValid() && NodeBlockingEMP->IsEMPInteractionActive());
	}
	return true;
}

bool AFirstPersonCharacter::BuildNodeInteractionRay(FVector& Start, FVector& Direction) const
{
	if (!CanUseNodeInteraction())
	{
		return false;
	}
	FRotator Rotation;
	GetController()->GetPlayerViewPoint(Start, Rotation);
	Direction = Rotation.Vector();
	const APlayerController* PC = Cast<APlayerController>(GetController());
	if (PC && PC->IsLocalController() && PC->bShowMouseCursor)
	{
		FVector MouseOrigin;
		if (!PC->DeprojectMousePositionToWorld(MouseOrigin, Direction))
		{
			return false;
		}
	}
	// 원점은 클라이언트/서버 모두 자신의 시점을 쓴다. 클라이언트의 임의 위치는 전송하지 않는다.
	return !Start.ContainsNaN() && !Direction.ContainsNaN() && Direction.Normalize()
		&& FVector::DotProduct(Rotation.Vector(), Direction)
			>= FMath::Cos(FMath::DegreesToRadians(FMath::Clamp(NodeMaxAimAngle, 1.0f, 89.0f)));
}

bool AFirstPersonCharacter::HasNodeInteractionPath(const AInteractionNode* Node,
	const FVector& Start, const FVector& End) const
{
	FCollisionQueryParams Params(SCENE_QUERY_STAT(NodeInteractionPath), false, this);
	Params.AddIgnoredActor(CurrentWeapon);
	Params.AddIgnoredActor(Node);
	FHitResult Hit;
	// UI와 몸체 선택은 분리해도 앞쪽 일반 상호작용/벽의 차폐는 우회하지 않는다.
	if (GetWorld()->LineTraceSingleByChannel(Hit, Start, End, InteractionVisibilityTraceChannel, Params))
	{
		return false;
	}
	return InteractionTraceChannel == InteractionVisibilityTraceChannel
		|| !GetWorld()->LineTraceSingleByChannel(Hit, Start, End, InteractionTraceChannel, Params);
}

AInteractionNode* AFirstPersonCharacter::FindNodeAlongRay(const FVector& Start, const FVector& Direction) const
{
	if (!CanUseNodeInteraction() || Start.ContainsNaN() || Direction.ContainsNaN() || !Direction.IsNormalized())
	{
		return nullptr;
	}
	const FVector End = Start + Direction * FMath::Max(InteractionAvailableRange, 0.0f);
	AInteractionNode* BestTarget = nullptr;
	double BestDistance = TNumericLimits<double>::Max();
	bool bBestPrecise = false;
	const FGameplayTagContainer InteractorTags = GetOwnedGameplayTagsForQuery();
	for (const TWeakObjectPtr<AInteractionNode>& Entry : InteractionNodes)
	{
		AInteractionNode* Node = Entry.Get();
		if (!Node || Node->GetWorld() != GetWorld() || Node->IsActorBeingDestroyed()
			|| !Node->GetInteractableComponent()->CanInteract(InteractorTags))
		{
			continue;
		}
		for (const TWeakObjectPtr<UPrimitiveComponent>& EntryBody : Node->GetInteractionBodies())
		{
			UPrimitiveComponent* Body = EntryBody.Get();
			if (!Node->IsInteractionBodyVisible(Body)
				|| Body->Bounds.GetBox().ComputeSquaredDistanceToPoint(Start) > FMath::Square(InteractionAvailableRange))
			{
				continue;
			}
			ActorBodyTargeting::FBodyHit Hit;
			FCollisionQueryParams Params(SCENE_QUERY_STAT(NodeInteractionBody), true);
			if (!ActorBodyTargeting::IntersectBody(*Body, Start, End, Params, Hit))
			{
				continue;
			}
			if (!HasNodeInteractionPath(Node, Start, Hit.Location) || !HasInteractionLineOfSight(Node, Body))
			{
				continue;
			}
			const double Distance = FVector::DistSquared(Start, Hit.Location);
			// 같은 거리에서는 이름순으로 고정해 캐시 순서에 따라 선택이 흔들리지 않게 한다.
			const int32 TieBreak = Distance == BestDistance && BestTarget
				? Node->GetPathName().Compare(BestTarget->GetPathName()) : 0;
			if (ActorBodyTargeting::IsBetterCandidate(Hit.bPrecise, Distance, bBestPrecise, BestDistance, TieBreak))
			{
				BestDistance = Distance;
				BestTarget = Node;
				bBestPrecise = Hit.bPrecise;
			}
		}
	}
	return BestTarget;
}

bool AFirstPersonCharacter::ValidateNodeStart(const AInteractionNode* Node, const FVector& Direction) const
{
	if (!IsValid(Node) || !CanUseNodeInteraction() || Direction.ContainsNaN() || !Direction.IsNormalized())
	{
		return false;
	}
	FVector Start;
	FRotator Rotation;
	GetController()->GetPlayerViewPoint(Start, Rotation);
	const double MinDot = FMath::Cos(FMath::DegreesToRadians(FMath::Clamp(NodeMaxAimAngle, 1.0f, 89.0f)));
	return FVector::DotProduct(Rotation.Vector(), Direction) >= MinDot && FindNodeAlongRay(Start, Direction) == Node;
}

bool AFirstPersonCharacter::CanMaintainNodeInteraction(const AInteractionNode* Node) const
{
	if (!IsValid(Node) || Node->IsActorBeingDestroyed() || Node->GetWorld() != GetWorld()
		|| !CanUseNodeInteraction() || !Node->GetInteractableComponent()->CanInteract(GetOwnedGameplayTagsForQuery()))
	{
		return false;
	}
	FVector Start;
	FRotator Rotation;
	GetController()->GetPlayerViewPoint(Start, Rotation);
	for (const TWeakObjectPtr<UPrimitiveComponent>& Entry : Node->GetInteractionBodies())
	{
		UPrimitiveComponent* Body = Entry.Get();
		if (Node->IsInteractionBodyVisible(Body)
			&& Body->Bounds.GetBox().ComputeSquaredDistanceToPoint(Start) <= FMath::Square(FMath::Max(InteractionAvailableRange, 0.0f))
			&& HasInteractionLineOfSight(Node, Body))
		{
			return true;
		}
	}
	return false;
}

AActor* AFirstPersonCharacter::FindInteractTargetByTrace(FVector* OutNodeAimDirection) const
{
	if (OutNodeAimDirection)
	{
		*OutNodeAimDirection = FVector::ZeroVector;
	}
	FVector Start;
	FVector Direction;
	AInteractionNode* Node = BuildNodeInteractionRay(Start, Direction) ? FindNodeAlongRay(Start, Direction) : nullptr;
	if (Node && OutNodeAimDirection)
	{
		*OutNodeAimDirection = Direction;
	}
	// 노드 Ray 앞의 일반 대상은 HasNodeInteractionPath에서 차단한다.
	// 노드를 선택하지 못한 경우에만 기존 일반 상호작용의 정면 Trace로 돌아간다.
	return Node ? Node : FindLegacyInteractTargetByTrace();
}

AActor* AFirstPersonCharacter::FindLegacyInteractTargetByTrace() const
{
	if (!GetWorld() || !GetController())
	{
		return nullptr;
	}

	FVector CameraLocation;
	FRotator CameraRotation;
	GetController()->GetPlayerViewPoint(CameraLocation, CameraRotation);

	const bool bSphereTrace =
		InteractionTraceMode == EInteractionTraceMode::SphereTrace;
	const float SphereRadius = FMath::Max(InteractionSphereTraceRadius, 0.0f);
	// 카메라에서 끝점까지의 거리이며, SphereTrace는 구 중심을 이 거리만큼 이동시킨다.
	const float TraceDistance = FMath::Max(InteractionAvailableRange, 0.0f);

	const FVector Start = CameraLocation;
	const FVector End = Start + CameraRotation.Vector() * TraceDistance;

	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(InteractionTrace), false, this);
	QueryParams.AddIgnoredActor(this);
	QueryParams.AddIgnoredActor(CurrentWeapon);

	FHitResult Hit;
	bool bHit = false;

	if (bSphereTrace)
	{
		bHit = GetWorld()->SweepSingleByChannel(
			Hit,
			Start,
			End,
			FQuat::Identity,
			InteractionTraceChannel,
			FCollisionShape::MakeSphere(SphereRadius),
			QueryParams
		);
	}
	else
	{
		bHit = GetWorld()->LineTraceSingleByChannel(
			Hit,
			Start,
			End,
			InteractionTraceChannel,
			QueryParams
		);
	}

	if (bDrawInteractionTrace)
	{
		const FColor TraceColor = bHit ? FColor::Cyan : FColor::Orange;

		if (bSphereTrace)
		{
			const FVector TraceVector = End - Start;
			const float TraceLength = TraceVector.Size();
			const FVector TraceDirection = TraceLength > UE_KINDA_SMALL_NUMBER
				? TraceVector / TraceLength
				: CameraRotation.Vector();
			const FVector CapsuleCenter = (Start + End) * 0.5f;
			const float CapsuleHalfHeight = TraceLength * 0.5f + SphereRadius;
			const FQuat CapsuleRotation =
				FQuat::FindBetweenNormals(FVector::UpVector, TraceDirection);

			DrawDebugCapsule(
				GetWorld(),
				CapsuleCenter,
				CapsuleHalfHeight,
				SphereRadius,
				CapsuleRotation,
				TraceColor,
				false,
				InteractionTraceInterval,
				0,
				1.5f
			);

			DrawDebugSphere(
				GetWorld(), Start, SphereRadius, 16,
				FColor::Yellow, false, InteractionTraceInterval, 0, 1.0f
			);
			DrawDebugSphere(
				GetWorld(), End, SphereRadius, 16,
				TraceColor, false, InteractionTraceInterval, 0, 1.0f
			);
			DrawDebugString(
				GetWorld(),
				CapsuleCenter + FVector(0.0f, 0.0f, 16.0f),
				TEXT("Interact SphereTrace"),
				nullptr,
				TraceColor,
				InteractionTraceInterval,
				true
			);
		}
		else
		{
			DrawDebugLine(
				GetWorld(),
				Start,
				End,
				TraceColor,
				false,
				InteractionTraceInterval,
				0,
				2.0f
			);

			DrawDebugPoint(
				GetWorld(), Start, 8.0f,
				FColor::Yellow, false, InteractionTraceInterval, 0
			);
			DrawDebugPoint(
				GetWorld(), End, 8.0f,
				TraceColor, false, InteractionTraceInterval, 0
			);
			DrawDebugString(
				GetWorld(),
				(Start + End) * 0.5f + FVector(0.0f, 0.0f, 16.0f),
				TEXT("Interact LineTrace"),
				nullptr,
				TraceColor,
				InteractionTraceInterval,
				true
			);
		}

		if (bHit)
		{
			DrawDebugPoint(
				GetWorld(), Hit.ImpactPoint, 12.0f,
				FColor::Cyan, false, InteractionTraceInterval, 0
			);
			DrawDebugString(
				GetWorld(),
				Hit.ImpactPoint + FVector(0.0f, 0.0f, 16.0f),
				TEXT("Interact Trace Hit"),
				nullptr,
				FColor::Cyan,
				InteractionTraceInterval,
				true
			);
		}
	}

	if (!bHit)
	{
		return nullptr;
	}

	AActor* HitActor = Hit.GetActor();
	// 노드가 큰 탐지 콜리전에 맞았다는 이유만으로 몸체 판정을 우회할 수 없다.
	if (Cast<AInteractionNode>(HitActor))
	{
		return nullptr;
	}
	IInteractableInterface* Interactable = Cast<IInteractableInterface>(HitActor);
	if (!Interactable)
	{
		return nullptr;
	}

	UInteractableComponent* InteractableComponent =
		Interactable->GetInteractableComponent();

	if (!InteractableComponent ||
		!InteractableComponent->CanInteract(GetOwnedGameplayTagsForQuery()))
	{
		return nullptr;
	}

	// 클라이언트 선택과 서버 실행 재검증 모두 동일한 시야 차단 규칙을 사용한다.
	if (!HasInteractionLineOfSight(HitActor, Hit.GetComponent()))
	{
		return nullptr;
	}

	return HitActor;
}
bool AFirstPersonCharacter::IsInteractTargetByTrace(AActor* TargetActor) const
{
	if (!TargetActor)
	{
		return false;
	}

	return FindInteractTargetByTrace() == TargetActor;
}

/*
void AFirstPersonCharacter::ArenaReload() 
{
	AFirstPersonPlayerController* FController = Cast<AFirstPersonPlayerController>(GetController());
	if (FController)
	{
		FController->Server_RequestArenaReload();
	}
}
*/

void AFirstPersonCharacter::SyncInteractableKeyWidgets(
	const TArray<AActor*>& CurrentInteractables)
{
	TSet<AActor*> CurrentSet;
	const FGameplayTagContainer OwnedGameplayTags = GetOwnedGameplayTagsForQuery();

	// Only usable actors retain an active interaction; locked actors stay in the range cache below.
	for (AActor* CurrentInteractable : CurrentInteractables)
	{
		if (IInteractableInterface* Interactable = Cast<IInteractableInterface>(CurrentInteractable))
		{
			if (UInteractableComponent* Component = Interactable->GetInteractableComponent();
				IsValid(Component) && Component->CanInteract(OwnedGameplayTags))
			{
				CurrentSet.Add(CurrentInteractable);
			}
		}
	}

	for (TObjectPtr<AActor> PreviousInteractable : NearbyInteractables)
	{
		if (!IsValid(PreviousInteractable) ||
			CurrentSet.Contains(PreviousInteractable.Get()))
		{
			continue;
		}

		if (IInteractableInterface* PreviousInterface = Cast<IInteractableInterface>(PreviousInteractable.Get()))
		{
			if (UInteractableComponent* PreviousComponent = PreviousInterface->GetInteractableComponent())
			{
				PreviousComponent->InteractKeyWidgetDeactivate();
				PreviousComponent->InteractionLockedWidgetDeactivate();
				const bool bIsActiveInteraction =
					FocusedInteractable == PreviousInteractable.Get() ||
					HoldingInteractActor == PreviousInteractable.Get();

				if (!bAwaitingHoldInteractResult &&
					bIsActiveInteraction &&
					PreviousComponent->RequiresHoldInteract())
				{
					AInteractionNode* Node = Cast<AInteractionNode>(PreviousInteractable.Get());
					// 범위 이탈로 로컬 시도를 정리하기 전에 취소 RPC의 번호를 보관한다.
					const int32 HoldAttemptId = Node ? Node->GetLocalHoldAttemptId(this) : 0;
					PreviousComponent->ResetHoldInteraction(this);
					if (Node) Node->EndLocalHoldAttempt(this, true);

					if (HoldingInteractActor == PreviousInteractable.Get())
					{
						HoldingInteractActor = nullptr;
					}

					ServerCancelHoldInteract(PreviousInteractable.Get(), HoldAttemptId);
				}

				PreviousComponent->DeactivateInteractionDesc();
			}
		}

		if (FocusedInteractable == PreviousInteractable.Get())
		{
			FocusedInteractable = nullptr;
		}
	}

	NearbyInteractables.Reset();

	for (AActor* CurrentInteractable : CurrentInteractables)
	{
		if (!IsValid(CurrentInteractable))
		{
			continue;
		}

		NearbyInteractables.Add(CurrentInteractable);

		if (IInteractableInterface* CurrentInterface =
			Cast<IInteractableInterface>(CurrentInteractable))
		{
			if (UInteractableComponent* CurrentComponent =
				CurrentInterface->GetInteractableComponent())
			{
				CurrentComponent->UpdateInnerRangeWidgets(this, CurrentInteractable == FocusedInteractable);
			}
		}
	}
}
