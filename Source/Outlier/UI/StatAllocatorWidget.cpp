#include "UI/StatAllocatorWidget.h"

#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Components/WidgetSwitcher.h"
#include "Drone/Partner/PartnerCharacter.h"
#include "GameFramework/PlayerController.h"
#include "OutlierPlayerState.h"
#include "Shooter/ShooterCharacter.h"

void UStatAllocatorWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();

	StartButton->OnClicked.AddUniqueDynamic(this, &UStatAllocatorWidget::HandleStartButtonClicked);
	ShowIntroPage();
}

void UStatAllocatorWidget::NativeConstruct()
{
	Super::NativeConstruct();
	BindOwningPlayerState();
}

void UStatAllocatorWidget::NativeDestruct()
{
	UnbindPlayerState();
	Super::NativeDestruct();
}

void UStatAllocatorWidget::InjectCharacters(
	AShooterCharacter* InShooterCharacter,
	APartnerCharacter* InPartnerCharacter)
{
	ShooterCharacter = InShooterCharacter;
	PartnerCharacter = InPartnerCharacter;

}

void UStatAllocatorWidget::InitializeUILayerContext_Implementation(
	const TArray<AActor*>& ContextActors)
{
	InjectCharacters(
		ContextActors.IsValidIndex(0)
			? Cast<AShooterCharacter>(ContextActors[0])
			: nullptr,
		ContextActors.IsValidIndex(1)
			? Cast<APartnerCharacter>(ContextActors[1])
			: nullptr);
	ShowIntroPage();
}

void UStatAllocatorWidget::ShowIntroPage()
{
	SetActivePage(IntroPageIndex);
}

void UStatAllocatorWidget::ShowAllocatorPage()
{
	SetActivePage(AllocatorPageIndex);
}

int32 UStatAllocatorWidget::GetCurrentPageIndex() const
{
	return StatAllocatorSwitcher
		? StatAllocatorSwitcher->GetActiveWidgetIndex()
		: INDEX_NONE;
}

bool UStatAllocatorWidget::HandleUILayerEscape_Implementation()
{
	if (GetCurrentPageIndex() == AllocatorPageIndex)
	{
		ShowIntroPage();
		return true;
	}

	return false;
}

bool UStatAllocatorWidget::HandleUILayerConfirmed_Implementation()
{
	if (GetCurrentPageIndex() == IntroPageIndex) //살짝 하드코딩.고정 인덱스니
	{
		//UE_LOG(LogTemp, Error, TEXT("HandleUILayerConfirmed_Called"));
		HandleStartButtonClicked();
	}
	else
	{
		return false;
	}

	return true;
}

void UStatAllocatorWidget::HandleStartButtonClicked()
{
	SetActivePage(AllocatorPageIndex);
}

void UStatAllocatorWidget::BindOwningPlayerState()
{
	const APlayerController* PlayerController = GetOwningPlayer();
	AOutlierPlayerState* PlayerState = PlayerController
		? PlayerController->GetPlayerState<AOutlierPlayerState>()
		: nullptr;

	if (BoundPlayerState.Get() == PlayerState && NodeCountChangedHandle.IsValid())
	{
		return;
	}

	UnbindPlayerState();
	BoundPlayerState = PlayerState;

	if (PlayerState)
	{
		NodeCountChangedHandle = PlayerState->OnNodeCountChanged.AddUObject(
			this,
			&UStatAllocatorWidget::HandleNodeCountChanged);
	}
}

void UStatAllocatorWidget::UnbindPlayerState()
{
	if (AOutlierPlayerState* PlayerState = BoundPlayerState.Get();
		PlayerState && NodeCountChangedHandle.IsValid())
	{
		PlayerState->OnNodeCountChanged.Remove(NodeCountChangedHandle);
	}

	NodeCountChangedHandle.Reset();
	BoundPlayerState.Reset();
}

void UStatAllocatorWidget::RefreshNodeCountText()
{
	if (!NodeCountText)
	{
		return;
	}

	const AOutlierPlayerState* PlayerState = BoundPlayerState.Get();
	NodeCountText->SetText(FText::AsNumber(
		PlayerState ? PlayerState->GetNodeCount() : 0));
}

void UStatAllocatorWidget::HandleNodeCountChanged(int32 NewNodeCount)
{
	NodeCountText->SetText(FText::AsNumber(NewNodeCount));
}

bool UStatAllocatorWidget::SetActivePage(int32 PageIndex)
{
	if (!StatAllocatorSwitcher
		|| PageIndex < 0
		|| PageIndex >= StatAllocatorSwitcher->GetNumWidgets())
	{
		return false;
	}

	StatAllocatorSwitcher->SetActiveWidgetIndex(PageIndex);

	if (PageIndex == AllocatorPageIndex)
	{
		BindOwningPlayerState();
		RefreshNodeCountText();
	}

	return true;
}
