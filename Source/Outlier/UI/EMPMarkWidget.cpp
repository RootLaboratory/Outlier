#include "UI/EMPMarkWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/SceneComponent.h"
#include "Drone/Partner/EMPFrameBillboardActor.h"
#include "Drone/Partner/PartnerEMPComponent.h"
#include "Engine/World.h"

void UEMPMarkWidget::InitializeMark(AActor* InTargetActor, UEMPableComponent* InEMPableComponent, UPartnerEMPComponent* InEMPComponent)
{
	TargetActor = InTargetActor;
	EMPableComponent = InEMPableComponent;
	EMPComponent = InEMPComponent;

	CacheTargetSceneComponent();
	SpawnBillboardFrame();
	AlignBillboardFrameToTarget();
}

void UEMPMarkWidget::NativeConstruct()
{
	Super::NativeConstruct();

	SetVisibility(ESlateVisibility::Visible);

	if (!SelectButton && WidgetTree)
	{
		SelectButton = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("SelectButton"));
		WidgetTree->RootWidget = SelectButton;
	}

	if (SelectButton)
	{
		SelectButton->SetVisibility(ESlateVisibility::Visible);
PRAGMA_DISABLE_DEPRECATION_WARNINGS
		SelectButton->IsFocusable = false;
PRAGMA_ENABLE_DEPRECATION_WARNINGS
		SelectButton->OnClicked.RemoveDynamic(this, &UEMPMarkWidget::HandleClicked);
		SelectButton->OnClicked.AddDynamic(this, &UEMPMarkWidget::HandleClicked);
	}
}

void UEMPMarkWidget::NativeDestruct()
{
	DestroyBillboardFrame();

	Super::NativeDestruct();
}

void UEMPMarkWidget::HandleClicked()
{
	if (!EMPComponent || !TargetActor)
	{
		return;
	}

	if (BillboardFrame)
	{
		//AlignBillboardFrameToTarget();
		BillboardFrame->PlayCollapse(CollaspsedTime);
		BillboardFrame->PlayFrameColorTransition(CollaspsedTime);
	}

	EMPComponent->TryMarkEMPTarget(TargetActor);
	EMPComponent->RefocusEMPInput();
}

void UEMPMarkWidget::CacheTargetSceneComponent()
{
	TargetSceneComponent = TargetActor ? TargetActor->GetRootComponent() : nullptr;
}

void UEMPMarkWidget::SpawnBillboardFrame()
{
	DestroyBillboardFrame();

	if (!BillboardFrameClass || !TargetSceneComponent)
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.Owner = TargetActor;
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	const FTransform SpawnTransform(TargetSceneComponent->GetComponentRotation(), TargetSceneComponent->GetComponentLocation());
	BillboardFrame = World->SpawnActor<AEMPFrameBillboardActor>(BillboardFrameClass, SpawnTransform, SpawnParameters);

	if (BillboardFrame)
	{
		BillboardFrame->AttachToComponent(TargetSceneComponent, FAttachmentTransformRules::KeepWorldTransform);
	}
}

void UEMPMarkWidget::DestroyBillboardFrame()
{
	if (BillboardFrame)
	{
		BillboardFrame->Destroy();
		BillboardFrame = nullptr;
	}
}

void UEMPMarkWidget::AlignBillboardFrameToTarget() const
{
	if (!BillboardFrame || !TargetSceneComponent)
	{
		return;
	}

	UE_LOG(LogTemp, Error, TEXT("AlignBillboardFrameToTarget"));

	BillboardFrame->SetCollapseAlpha(0.5f);
	BillboardFrame->SetActorLocation(TargetSceneComponent->GetComponentLocation());
}
