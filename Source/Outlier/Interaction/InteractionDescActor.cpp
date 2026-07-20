#include "Interaction/InteractionDescActor.h"

#include "Components/BoxComponent.h"
#include "Components/SceneComponent.h"
#include "Components/WidgetComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/LocalPlayer.h"
#include "FirstPerson/FirstPersonCharacter.h"
#include "GameFramework/PlayerController.h"
#include "Interaction/InteractionNode.h"
#include "Interaction/InteractableComponent.h"
#include "Interaction/InteractInfoSubsystem.h"
#include "UI/InteractionDescWidget.h"

AInteractionDescActor::AInteractionDescActor()
{
	PrimaryActorTick.bCanEverTick = true;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	RootComponent = SceneRoot;

	InteractableComponent = CreateDefaultSubobject<UInteractableComponent>(TEXT("InteractableComponent"));

	TraceCollision = CreateDefaultSubobject<UBoxComponent>(TEXT("TraceCollision"));
	TraceCollision->SetupAttachment(SceneRoot);
	TraceCollision->SetBoxExtent(TraceCollisionExtent);
	TraceCollision->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	TraceCollision->SetCollisionObjectType(ECC_WorldDynamic);
	TraceCollision->SetCollisionResponseToAllChannels(ECR_Ignore);
	TraceCollision->SetGenerateOverlapEvents(false);

	DescWidgetComponent = CreateDefaultSubobject<UWidgetComponent>(TEXT("DescWidgetComponent"));
	DescWidgetComponent->SetupAttachment(SceneRoot);
	DescWidgetComponent->SetWidgetSpace(EWidgetSpace::World);
	DescWidgetComponent->SetDrawSize(InteractionDescWidgetDrawSize);
	DescWidgetComponent->SetDrawAtDesiredSize(false);
	DescWidgetComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	DescWidgetComponent->SetGenerateOverlapEvents(false);
	DescWidgetComponent->SetRelativeScale3D(InteractionDescWidgetScale);
	DescWidgetComponent->SetVisibility(false);
}

void AInteractionDescActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	BillboardToCamera();

	if (bDrawTraceCollisionDebug && TraceCollision)
	{
		DrawDebugBox(
			GetWorld(),
			TraceCollision->GetComponentLocation(),
			TraceCollision->GetScaledBoxExtent(),
			TraceCollision->GetComponentQuat(),
			FColor::Cyan,
			false,
			0.0f,
			0,
			2.0f
		);
	}
}

void AInteractionDescActor::SetSourceInteractionNode(AInteractionNode* SourceNode)
{
	SourceInteractionNode = SourceNode;
}

void AInteractionDescActor::ActivateDesc(AFirstPersonCharacter* Interactor)
{
	ActivateDescFromSource(Interactor, InteractableComponent);
}

void AInteractionDescActor::ActivateDescFromSource(AFirstPersonCharacter* Interactor, const UInteractableComponent* SourceInteractableComponent)
{
	if (!Interactor || !Interactor->IsLocallyControlled() || !DescWidgetComponent)
	{
		UE_LOG(LogTemp, Error, TEXT("!DESCWIDGETCOMPONENT"));
		return;
	}

	if (!InteractionDescWidgetClass)
	{
		UE_LOG(LogTemp, Error, TEXT("!InteractionDescWidgetClass"));

		return;
	}

	APlayerController* PlayerController = Cast<APlayerController>(Interactor->GetController());
	if (!PlayerController)
	{
		return;
	}

	DescWidgetComponent->SetWidgetClass(InteractionDescWidgetClass);
	DescWidgetComponent->SetDrawSize(InteractionDescWidgetDrawSize);

	if (ULocalPlayer* LocalPlayer = PlayerController->GetLocalPlayer())
	{
		DescWidgetComponent->SetOwnerPlayer(LocalPlayer);
	}

	if (!DescWidgetComponent->GetUserWidgetObject())
	{
		DescWidgetComponent->InitWidget();
	}

	InteractionDescWidget = Cast<UInteractionDescWidget>(DescWidgetComponent->GetUserWidgetObject());

	FGameplayTag InteractTag;
	FInteractInfoRow InteractInfo;
	const UGameInstance* GameInstance = GetWorld() ? GetWorld()->GetGameInstance() : nullptr;
	const UInteractInfoSubsystem* InteractInfoSubsystem = GameInstance ? GameInstance->GetSubsystem<UInteractInfoSubsystem>() : nullptr;

	if (!GetPrimaryInteractTag(SourceInteractableComponent, InteractTag))
	{
		UE_LOG(LogTemp, Error, TEXT("[InteractionDescActor] No valid interact tag. Actor=%s SourceComponent=%s SourceTags=%s"),
			*GetName(),
			*GetNameSafe(SourceInteractableComponent),
			SourceInteractableComponent ? *SourceInteractableComponent->InteractableTags.ToStringSimple() : TEXT("None"));

		return;
	}

	if (!InteractInfoSubsystem)
	{
		UE_LOG(LogTemp, Error, TEXT("[InteractionDescActor] InteractInfoSubsystem is null. Actor=%s Tag=%s"),
			*GetName(),
			*InteractTag.ToString());

		return;
	}

	if (!InteractInfoSubsystem->TryGetInteractInfo(InteractTag, InteractInfo))
	{
		UE_LOG(LogTemp, Error, TEXT("[InteractionDescActor] Interact info row not found. Actor=%s Tag=%s"),
			*GetName(),
			*InteractTag.ToString());

		return;
	}

	if (InteractableComponent)
	{
		InteractableComponent->InteractKeyWidgetDeactivate();
	}

	DescWidgetComponent->SetVisibility(true);

	if (InteractionDescWidget)
	{
		InteractionDescWidget->UpdateInteractionDesc(InteractTag, InteractInfo, Progress);
	}
}

void AInteractionDescActor::DeactivateDesc()
{
	if (DescWidgetComponent)
	{
		DescWidgetComponent->SetVisibility(false);
	}

	if (InteractionDescWidget)
	{
		InteractionDescWidget->ClearInteractionDesc();
	}
}

void AInteractionDescActor::SetProgress(float InProgress)
{
	Progress = FMath::Clamp(InProgress, 0.0f, 1.0f);

	if (InteractionDescWidget)
	{
		InteractionDescWidget->SetProgress(Progress);
	}
}

void AInteractionDescActor::BillboardToCamera()
{
	if (!DescWidgetComponent || !GetWorld())
	{
		return;
	}

	const APlayerController* PlayerController = GetWorld()->GetFirstPlayerController();
	const APlayerCameraManager* CameraManager = PlayerController ? PlayerController->PlayerCameraManager : nullptr;
	if (!CameraManager)
	{
		return;
	}

	const FVector WidgetLocation = DescWidgetComponent->GetComponentLocation();
	const FVector CameraLocation = CameraManager->GetCameraLocation();
	const FVector ToCamera = CameraLocation - WidgetLocation;

	if (ToCamera.IsNearlyZero())
	{
		return;
	}

	DescWidgetComponent->SetWorldRotation(ToCamera.Rotation());
}

bool AInteractionDescActor::GetPrimaryInteractTag(const UInteractableComponent* SourceInteractableComponent, FGameplayTag& OutInteractTag) const
{
	if (!SourceInteractableComponent)
	{
		return false;
	}

	const FGameplayTag TargetRootTag = FGameplayTag::RequestGameplayTag(FName(TEXT("Interact.Target")), false);
	TArray<FGameplayTag> InteractableTagArray;
	SourceInteractableComponent->InteractableTags.GetGameplayTagArray(InteractableTagArray);

	FGameplayTag FirstValidTag;
	for (const FGameplayTag& Tag : InteractableTagArray)
	{
		if (Tag.IsValid())
		{
			if (!FirstValidTag.IsValid())
			{
				FirstValidTag = Tag;
			}

			if (TargetRootTag.IsValid() && Tag.MatchesTag(TargetRootTag))
			{
				OutInteractTag = Tag;
				return true;
			}
		}
	}

	if (FirstValidTag.IsValid())
	{
		OutInteractTag = FirstValidTag;
		return true;
	}

	return false;
}
