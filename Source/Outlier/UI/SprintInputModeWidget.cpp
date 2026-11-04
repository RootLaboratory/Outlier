#include "UI/SprintInputModeWidget.h"

#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "GameFramework/Character.h"
#include "OutlierPlayerState.h"
#include "Settings/LocalPlayerSettingsSubsystem.h"

void USprintInputModeWidget::NativeConstruct()
{
	Super::NativeConstruct();
	BindButtons();
	RefreshFromSettings();
}

void USprintInputModeWidget::NativePreConstruct()
{
	Super::NativePreConstruct();
	RefreshFromSettings();
}

void USprintInputModeWidget::NativeDestruct()
{
	SetSettingsContext(nullptr, EOutlierPlayerRole::None, false);
	UButton* Buttons[] = { SprintModeLeftButton.Get(), SprintModeRightButton.Get() };
	for (UButton* Button : Buttons)
	{
		if (!Button) continue;
		Button->OnPressed.RemoveAll(this);
		Button->OnClicked.RemoveAll(this);
	}
	Super::NativeDestruct();
}

void USprintInputModeWidget::BindButtons()
{
	UButton* Buttons[] = { SprintModeLeftButton.Get(), SprintModeRightButton.Get() };
	for (UButton* Button : Buttons)
	{
		if (!Button) continue;
		Button->OnPressed.AddUniqueDynamic(this, &ThisClass::HandlePressed);
		Button->OnClicked.AddUniqueDynamic(this, &ThisClass::HandleClicked);
	}
}

void USprintInputModeWidget::SetSettingsContext(ULocalPlayerSettingsSubsystem* Settings, EOutlierPlayerRole Role, bool bActive)
{
	if (BoundSettingsSubsystem != Settings)
	{
		if (BoundSettingsSubsystem) BoundSettingsSubsystem->OnSprintInputModeChanged.RemoveAll(this);
		BoundSettingsSubsystem = Settings;
		if (BoundSettingsSubsystem)
			BoundSettingsSubsystem->OnSprintInputModeChanged.AddUniqueDynamic(this, &ThisClass::HandleModeChanged);
	}
	// 역할/페이지 전환 전에 시작한 클릭을 무효화한다. 표시 갱신은 설정을 저장하지 않는다.
	PressRevision = MAX_uint32;
	DisplayedRole = Role;
	bInteractionActive = bActive;
	RefreshFromSettings();
}

void USprintInputModeWidget::RefreshFromSettings()
{
	SetVisibility(IsDesignTime() || DisplayedRole != EOutlierPlayerRole::None
		? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
	if (!SprintModeText) return;
	const EOutlierSprintInputMode Mode = BoundSettingsSubsystem
		? BoundSettingsSubsystem->GetSprintInputMode(DisplayedRole) : EOutlierSprintInputMode::Toggle;
	SprintModeText->SetText(FText::FromString(Mode == EOutlierSprintInputMode::Hold ? TEXT("Hold") : TEXT("Toggle")));
}

UWidget* USprintInputModeWidget::GetKeyboardFocusTarget() const
{
	return IsVisible() ? SprintModeLeftButton.Get() : nullptr;
}

bool USprintInputModeWidget::CycleMode()
{
	return BoundSettingsSubsystem && CycleMode(BoundSettingsSubsystem->GetInputSettingsContextRevision());
}

bool USprintInputModeWidget::CycleMode(uint32 ExpectedRevision)
{
	if (!BoundSettingsSubsystem || !bInteractionActive || DisplayedRole == EOutlierPlayerRole::None
		|| ExpectedRevision != BoundSettingsSubsystem->GetInputSettingsContextRevision()
		|| DisplayedRole != BoundSettingsSubsystem->GetInputSettingsRole()) return false;
	const EOutlierSprintInputMode Mode = BoundSettingsSubsystem->GetSprintInputMode(DisplayedRole);
	// 두 선택지는 좌/우 어느 쪽으로도 반대 값으로 순환한다. 저장은 기존 로컬 설정 경로를 사용한다.
	return BoundSettingsSubsystem->SetSprintInputMode(DisplayedRole,
		Mode == EOutlierSprintInputMode::Toggle ? EOutlierSprintInputMode::Hold : EOutlierSprintInputMode::Toggle);
}

void USprintInputModeWidget::HandlePressed()
{
	// Press와 Click 사이의 역할/소유자 변경도 검사한다.
	PressRevision = BoundSettingsSubsystem ? BoundSettingsSubsystem->GetInputSettingsContextRevision() : MAX_uint32;
}

void USprintInputModeWidget::HandleClicked()
{
	CycleMode(PressRevision);
	PressRevision = MAX_uint32;
}

void USprintInputModeWidget::HandleModeChanged(EOutlierPlayerRole Role, EOutlierSprintInputMode Mode)
{
	if (Role == DisplayedRole) RefreshFromSettings();
}
