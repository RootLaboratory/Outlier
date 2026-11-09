#include "Interaction/InteractionDescActor.h"

#include "Components/BoxComponent.h"
#include "Components/SceneComponent.h"
#include "Components/WidgetComponent.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "UI/ActorBasedWidgetSubsystem.h"
#include "FirstPerson/FirstPersonCharacter.h"
#include "GameFramework/PlayerController.h"
#include "Interaction/InteractableComponent.h"
#include "Interaction/InteractInfoSubsystem.h"
#include "UI/InteractionDescWidget.h"
#include "UI/TargetPointHudWidget.h"

AInteractionDescActor::AInteractionDescActor()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;

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
	DescWidgetComponent->SetDrawAtDesiredSize(true);
	DescWidgetComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	DescWidgetComponent->SetGenerateOverlapEvents(false);
	DescWidgetComponent->SetIsReplicated(false);
	DescWidgetComponent->SetRelativeScale3D(InteractionDescWidgetScale);
	DescWidgetComponent->SetVisibility(false);
}

void AInteractionDescActor::BeginPlay()
{
	Super::BeginPlay();
	// BP 호환용 컴포넌트는 보관하되 월드에는 렌더링하지 않는다.
	DescWidgetComponent->SetVisibility(false);
	if (UActorBasedWidgetSubsystem* Subsystem = GetWorld()->GetSubsystem<UActorBasedWidgetSubsystem>())
	{
		Subsystem->RegisterWidgetOwner(this);
	}
}

void AInteractionDescActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UActorBasedWidgetSubsystem* Subsystem = GetWorld()->GetSubsystem<UActorBasedWidgetSubsystem>())
	{
		Subsystem->UnregisterWidgetOwner(this);
	}
	DeactivateDesc();
	if (HudLayer) { HudLayer->Release(); HudLayer = nullptr; }
	Super::EndPlay(EndPlayReason);
}

void AInteractionDescActor::DismissActorBasedWidgets_Implementation()
{
	DeactivateDesc();
}

void AInteractionDescActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	UpdateHudPresentation();

}

void AInteractionDescActor::ActivateDesc(AFirstPersonCharacter* Interactor)
{
	ActivateDescFromSource(Interactor, InteractableComponent);
}

void AInteractionDescActor::ActivateDescFromSource(AFirstPersonCharacter* Interactor, UInteractableComponent* SourceInteractableComponent)
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

	// 기존 DescActor BP의 위젯 클래스와 공개 호출 경로를 그대로 재사용한다.
	if (!HudLayer) HudLayer = UTargetPointHudWidget::CreateForPlayer(PlayerController, this);
	if (!HudLayer) return;
	if (!InteractionDescWidget)
	{
		InteractionDescWidget = CreateWidget<UInteractionDescWidget>(PlayerController, InteractionDescWidgetClass);
		HudLayer->AddContent(InteractionDescWidget);
	}
	if (!InteractionDescWidget) return;
	SourceInteractable = SourceInteractableComponent;

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

	if (SourceInteractableComponent)
	{
		SourceInteractableComponent->InteractKeyWidgetDeactivate();
	}

	bDescShown = true;
	SetActorTickEnabled(true);

	if (InteractionDescWidget)
	{
		InteractionDescWidget->UpdateInteractionDesc(InteractTag, InteractInfo, Progress);
		RefreshDescWidgetDrawSize();
		UpdateHudPresentation();
	}
}

void AInteractionDescActor::DeactivateDesc()
{
	bDescShown = false;
	SetActorTickEnabled(false);
	UpdateHudPresentation();
	if (InteractionDescWidget) InteractionDescWidget->ClearInteractionDesc();
}

void AInteractionDescActor::SetProgress(float InProgress)
{
	Progress = FMath::Clamp(InProgress, 0.0f, 1.0f);

	if (InteractionDescWidget)
	{
		InteractionDescWidget->SetProgress(Progress);
	}
}

void AInteractionDescActor::PopupAnimationCall(bool Flag)
{
	if (!InteractionDescWidget)
	{
		return;
	}

	InteractionDescWidget->PlayPopUp(Flag);

}

void AInteractionDescActor::UpdateHudPresentation()
{
	if (!HudLayer || !InteractionDescWidget) return;
	const UInteractableComponent* Source = SourceInteractable.Get();
	const FVector Point = Source ? Source->GetInteractionWidgetLocation() : GetActorLocation();
	HudLayer->UpdateContent(InteractionDescWidget, bDescShown && Source != nullptr, Point, InteractionDescWidgetDrawSize);
}

void AInteractionDescActor::RefreshDescWidgetDrawSize()
{
	if (InteractionDescWidget)
	{
		InteractionDescWidget->InvalidateLayoutAndVolatility();
		InteractionDescWidget->ForceLayoutPrepass();
	}
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
