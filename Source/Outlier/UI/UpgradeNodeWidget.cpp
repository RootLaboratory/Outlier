#include "UI/UpgradeNodeWidget.h"

#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Components/PanelWidget.h"
#include "Engine/DataTable.h"
#include "Engine/Texture.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "FirstPerson/FirstPersonPlayerController.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "OutlierPlayerState.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Misc/ScopeExit.h"
#include "Framework/Application/SlateApplication.h"
#include "UI/UpgradeDescWidget.h"
#include "UI/UpgradeNodeGroupWidget.h"
#include "Upgrade/OutlierUpgradeComponent.h"
#include "Upgrade/OutlierUpgradeSetData.h"
#include "HAL/PlatformTime.h"
#include "Widgets/SWidget.h"

namespace
{
	const FName ColorParameterName(TEXT("Color"));
	const FName IntensityParameterName(TEXT("Intensity"));
	const FName TextureParameterName(TEXT("Texture"));
	const FName NodeTextureParameterName(TEXT("NodeTexture"));
	const FName ActivatedTextureParameterName(TEXT("ActivatedTexture"));
	const FName UseActivatedTextureParameterName(TEXT("UseActivatedTexture"));
	const FName UnlockedColorParameterName(TEXT("UnlockedColor"));
	const FName ActivatedColorParameterName(TEXT("ActivatedColor"));
	// Unity ��忡�� �ٸ� UI cpp�� �͸� namespace ����� �浹���� �ʵ��� �����Ѵ�.
	const FName UpgradeNodeProgressParameterName(TEXT("Progress"));
}

void UUpgradeNodeWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();

	BindButtonEvents();
}

void UUpgradeNodeWidget::NativeConstruct()
{
	Super::NativeConstruct();
	ResetUnlockPresentation();

	BindUpgradeContextEvents();
	if (IsValid(CachedUpgradeComponent))
	{
		RefreshFromUpgradeComponent(CachedUpgradeComponent);
	}
	RefreshNodeAppearance();
	RefreshNodeTexture();
}

void UUpgradeNodeWidget::SynchronizeProperties()
{
	Super::SynchronizeProperties();

	BindButtonEvents();
	RefreshNodeRowNameFromIds();
	if (CachedUpgradeComponent)
	{
		RefreshFromUpgradeComponent(CachedUpgradeComponent);
	}
	RefreshEnabledState();
	RefreshNodeAppearance();
	RefreshNodeTexture();
}

void UUpgradeNodeWidget::NativeDestruct()
{
	ResetUnlockPresentation();
	SetNodeHovered(false);
	ResetUpgradeInteraction();
	UnbindUpgradeContextEvents();
	HideUpgradeDescWidget();

	if (CachedUpgradeDescWidget && CachedUpgradeDescWidget->GetParent())
	{
		CachedUpgradeDescWidget->RemoveFromParent();
	}
	CachedUpgradeDescWidget = nullptr;
	bHoverDescVisible = false;

	Super::NativeDestruct();
}

void UUpgradeNodeWidget::InjectNodeData(FName InNodeRowName, const FOutlierUpgradeNodeRow& InNodeData)
{
	if (CurrentNodeRowName != InNodeRowName || TreeId != InNodeData.TreeId || NodeId != InNodeData.NodeId
		|| CurrentNodeData.Role != InNodeData.Role)
	{
		ResetUpgradeInteraction();
		ResetUnlockPresentation();
	}
	CurrentNodeRowName = InNodeRowName;
	CurrentNodeData = InNodeData;
	NodeRowName = InNodeRowName;
	TreeId = InNodeData.TreeId;
	NodeId = InNodeData.NodeId;
}

void UUpgradeNodeWidget::InjectViewData(const FOutlierUpgradeNodeViewData& InViewData)
{
	InjectNodeData(InViewData.RowName, InViewData.NodeRow);
	CurrentNodeCount = InViewData.CurrentNodeCount;
	SetNodeState(InViewData.State, InViewData.bCanAfford);
	RefreshVisibleDescWidget();
}

void UUpgradeNodeWidget::RefreshFromUpgradeComponent(UOutlierUpgradeComponent* UpgradeComponent)
{
	if (!UpgradeComponent)
	{
		return;
	}

	if (CachedUpgradeComponent != UpgradeComponent)
	{
		SetUpgradeComponent(UpgradeComponent);
		return;
	}

	// ���� ������Ʈ�� �����ص� ������ ������ ��ü�Ǹ� ���� ȭ���� �Է�/������ �̾���� �ʴ´�.
	if (CachedUpgradeSetData.Get() != UpgradeComponent->GetUpgradeSetData())
	{
		ResetUpgradeInteraction();
		ResetUnlockPresentation();
		CachedUpgradeSetData = UpgradeComponent->GetUpgradeSetData();
	}
	const FName LookupName = GetNodeLookupName();

	FOutlierUpgradeNodeViewData ViewData;
	if (!LookupName.IsNone() && UpgradeComponent->GetNodeViewData(LookupName, ViewData))
	{
		ApplyInjectedPlayerState(ViewData);
		InjectViewData(ViewData);
	}
	else
	{
		ResetUpgradeInteraction();
		SetNodeState(EOutlierUpgradeNodeState::Locked, false);
	}
}

void UUpgradeNodeWidget::SetUpgradeComponent(UOutlierUpgradeComponent* InUpgradeComponent)
{
	if (CachedUpgradeComponent != InUpgradeComponent)
	{
		ResetUpgradeInteraction();
		ResetUnlockPresentation();
		UnbindUpgradeContextEvents();
	}
	CachedUpgradeComponent = InUpgradeComponent;
	BindUpgradeContextEvents();
	RefreshNodeRowNameFromIds();
	RefreshFromUpgradeComponent(CachedUpgradeComponent);
}

void UUpgradeNodeWidget::SetUpgradePlayerState(AOutlierPlayerState* InPlayerState)
{
	if (CachedPlayerState != InPlayerState)
	{
		ResetUpgradeInteraction();
		ResetUnlockPresentation();
		UnbindUpgradeContextEvents();
	}
	CachedPlayerState = InPlayerState;
	BindUpgradeContextEvents();

	if (CachedUpgradeComponent)
	{
		RefreshFromUpgradeComponent(CachedUpgradeComponent);
	}
}

bool UUpgradeNodeWidget::RefreshNodeRowNameFromIds()
{
	FName ResolvedRowName = NAME_None;
	if (!TryResolveNodeRowNameByTreeAndNodeId(TreeId, NodeId, ResolvedRowName))
	{
		return false;
	}

	NodeRowName = ResolvedRowName;
	return true;
}

void UUpgradeNodeWidget::SetUpgradeDescWidget(UUpgradeDescWidget* InUpgradeDescWidget)
{
	CachedUpgradeDescWidget = InUpgradeDescWidget;
}

void UUpgradeNodeWidget::SetUpgradeDescWidgetClass(TSubclassOf<UUpgradeDescWidget> InUpgradeDescWidgetClass)
{
	if (InUpgradeDescWidgetClass)
	{
		UpgradeDescWidgetClass = InUpgradeDescWidgetClass;
	}
}

void UUpgradeNodeWidget::DismissDescription()
{
	if (bHoverDescVisible || CachedUpgradeDescWidget)
	{
		if (CachedUpgradeDescWidget)
		{
			CachedUpgradeDescWidget->ClearNodeData();
		}
		bHoverDescVisible = false;
	}
}

void UUpgradeNodeWidget::SetNodeState(EOutlierUpgradeNodeState InState, bool bInCanAfford)
{
	const bool bJustPurchased = CurrentState != EOutlierUpgradeNodeState::Activated
		&& InState == EOutlierUpgradeNodeState::Activated;
	const uint32 RestoreRevision = CachedPlayerState
		? CachedPlayerState->GetUpgradeRestoreRevision(CurrentNodeData.Role) : 0;
	const bool bRestored = bUnlockStateInitialized && RestoreRevision != UnlockRestoreRevision;
	if (bRestored || InState != EOutlierUpgradeNodeState::Activated)
	{
		StopUnlockPlayback();
	}
	// ���� ����/������ ��� ǥ���Ѵ�. ȭ���� ���� �ִ� ������ ���� �ű� Ȱ��ȭ�� ����Ѵ�.
	if (bJustPurchased && bUnlockStateInitialized && !bRestored && !IsDesignTime()
		&& bUseUnlockPresentation && !bAlwaysShowUnlockedIcon && EnsureUnlockMaterial())
	{
		CacheInteractionVisibility();
		UnlockVisibilityWidgets = InteractionVisibilityWidgets;
		if (IsUnlockVisible())
		{
			bUnlockPlaying = true;
			UnlockElapsed = 0.0f;
			UnlockMaterial->SetScalarParameterValue(TEXT("FrameIndex"), 0.0f);
			LastUnlockTime = FPlatformTime::Seconds();
			UnlockTickerHandle = FTSTicker::GetCoreTicker().AddTicker(
				FTickerDelegate::CreateUObject(this, &UUpgradeNodeWidget::TickUnlockPlayback), 1.0f / 60.0f);
		}
	}
	bUnlockStateInitialized = true;
	UnlockRestoreRevision = RestoreRevision;
	if (bJustPurchased && bHoverDescVisible)
	{
		HideUpgradeDescWidget();
		bHoverDescVisible = false;
		bSuppressDescUntilUnhover = NodeButton && NodeButton->IsHovered();
	}

	CurrentState = InState;
	bCanAfford = bInCanAfford;
	if (CurrentState == EOutlierUpgradeNodeState::Activated)
	{
		// ���� ���亸�� ���� ������ ���� �͵� ���⼭ �Ϸ��Ѵ�. ���� ���� ������ ���õȴ�.
		ClearPendingRequest();
		CancelHold();
	}
	else if (CurrentState != EOutlierUpgradeNodeState::Unlocked || !bCanAfford)
	{
		CancelHold();
	}
	RefreshEnabledState();
	RefreshNodeAppearance();
	RefreshNodeTexture();

	if (bHoverDescVisible && !ShouldShowDescOnHover())
	{
		HideUpgradeDescWidget();
		bHoverDescVisible = false;
	}
}

void UUpgradeNodeWidget::SetUnlockedNodeTexture(UTexture2D* InUnlockedNodeTexture)
{
	UnlockedNodeTexture = InUnlockedNodeTexture;
	RefreshNodeTexture();
}

void UUpgradeNodeWidget::SetDeactivatedNodeTexture(UTexture2D* InDeactivatedNodeTexture)
{
	DeactivatedNodeTexture = InDeactivatedNodeTexture;
	RefreshNodeTexture();
}

void UUpgradeNodeWidget::SetNodeTexture(UTexture2D* InNodeTexture)
{
	NodeTexture = InNodeTexture;
	RefreshNodeTexture();
}

bool UUpgradeNodeWidget::GetNodeData(FOutlierUpgradeNodeRow& OutNodeData) const
{
	if (CurrentNodeRowName.IsNone() && CurrentNodeData.NodeId.IsNone())
	{
		OutNodeData = FOutlierUpgradeNodeRow();
		return false;
	}

	OutNodeData = CurrentNodeData;
	return true;
}

void UUpgradeNodeWidget::HandlePressed()
{
	if (bHoldingUpgrade || IsUpgradeRequestPending())
	{
		return;
	}

	UOutlierUpgradeComponent* UpgradeComponent = ResolveUpgradeComponent();
	if (!IsValid(UpgradeComponent) || !IsValid(UpgradeComponent->GetOwner()))
	{
		return;
	}
	RefreshFromUpgradeComponent(UpgradeComponent);
	UpgradeController = Cast<AFirstPersonPlayerController>(GetOwningPlayer());
	if (!CachedPlayerState && UpgradeController.IsValid())
	{
		SetUpgradePlayerState(UpgradeController->GetPlayerState<AOutlierPlayerState>());
	}
	if (!CanBeginHold() || !UpgradeController.IsValid()
		|| !NodeButton || !NodeButton->IsHovered()
		|| !FSlateApplication::IsInitialized() || !FSlateApplication::Get().IsActive())
	{
		return;
	}

	CacheInteractionVisibility();
	if (!IsInteractionVisible())
	{
		return;
	}
	bHoldingUpgrade = true;
	HoldProgress = 0.0f;
	RefreshNodeFill();
	StartInteractionTicker();
}

void UUpgradeNodeWidget::HandleReleased()
{
	CancelHold();
}

bool UUpgradeNodeWidget::CanBeginHold() const
{
	return !bAlwaysShowUnlockedIcon && !IsUpgradeRequestPending()
		&& CurrentState == EOutlierUpgradeNodeState::Unlocked && bCanAfford
		&& IsValid(CachedUpgradeComponent) && !CurrentNodeRowName.IsNone();
}

bool UUpgradeNodeWidget::AdvanceHold(float DeltaSeconds)
{
	if (!bHoldingUpgrade)
	{
		return false;
	}
	if (!CanBeginHold())
	{
		CancelHold();
		return false;
	}

	const float Duration = FMath::IsFinite(HoldDuration) ? FMath::Max(0.01f, HoldDuration) : 1.0f;
	if (FMath::IsFinite(DeltaSeconds) && DeltaSeconds > 0.0f)
	{
		HoldProgress = FMath::Min(1.0f, HoldProgress + DeltaSeconds / Duration);
	}
	RefreshNodeFill();
	if (HoldProgress < 1.0f)
	{
		return false;
	}

	// �ϷḦ ���� �Һ��� �� ��û�Ѵ�. ���� �������� ������ ���� ������ ���Ÿ� �ٽ� ������ �ʴ´�.
	bHoldingUpgrade = false;
	return true;
}

void UUpgradeNodeWidget::CancelHold()
{
	bHoldingUpgrade = false;
	if (!IsUpgradeRequestPending())
	{
		HoldProgress = 0.0f;
		StopInteractionTicker();
		InteractionVisibilityWidgets.Reset();
	}
	RefreshNodeFill();
}

void UUpgradeNodeWidget::ClearPendingRequest()
{
	if (AFirstPersonPlayerController* Controller = UpgradeController.Get())
	{
		Controller->OnUpgradePurchaseResult.Remove(UpgradeResultHandle);
	}
	UpgradeResultHandle.Reset();
	PendingRequestId = 0;
	PendingNodeRowName = NAME_None;
	PendingUpgradeOwner.Reset();
	RequestStartedTime = 0.0;
}

void UUpgradeNodeWidget::ResetUpgradeInteraction()
{
	ClearPendingRequest();
	CancelHold();
}

void UUpgradeNodeWidget::RequestUpgrade()
{
	UOutlierUpgradeComponent* UpgradeComponent = CachedUpgradeComponent;
	AFirstPersonPlayerController* Controller = UpgradeController.Get();
	if (!IsValid(UpgradeComponent) || !IsValid(Controller))
	{
		ResetUpgradeInteraction();
		return;
	}

	// 누르는 사이 비용이나 선행 조건이 바뀌었을 수 있다. 전송 직전 최신 상태로 다시 검사한다.
	RefreshFromUpgradeComponent(UpgradeComponent);
	if (!CanBeginHold() || HoldProgress < 1.0f || !IsValid(UpgradeComponent->GetOwner()))
	{
		ResetUpgradeInteraction();
		return;
	}

	const UUpgradeNodeGroupWidget* NodeGroup = FindOwningNodeGroupWidget();
	UOutlierUpgradeSetData* SetData = NodeGroup ? NodeGroup->GetUpgradeSetData() : UpgradeComponent->GetUpgradeSetData();
	PendingRequestId = Controller->AllocateUpgradeRequestId();
	PendingNodeRowName = CurrentNodeRowName;
	PendingUpgradeOwner = UpgradeComponent->GetOwner();
	RequestStartedTime = FPlatformTime::Seconds();
	HoldProgress = 1.0f;
	RefreshNodeFill();
	UpgradeResultHandle = Controller->OnUpgradePurchaseResult.AddUObject(this, &UUpgradeNodeWidget::HandleUpgradeResult);

	// Listen 호스트는 이 호출 안에서 바로 응답/상태 변경을 받을 수 있으므로 대기를 먼저 등록한다.
	Controller->ServerTryActivateUpgradeNode(PendingUpgradeOwner.Get(), PendingNodeRowName, SetData, PendingRequestId);
}

void UUpgradeNodeWidget::HandleUpgradeResult(uint32 RequestId, AActor* UpgradeOwner, FName RowName, bool bSucceeded)
{
	if (!IsUpgradeRequestPending() || PendingRequestId != RequestId
		|| PendingUpgradeOwner.Get() != UpgradeOwner || PendingNodeRowName != RowName)
	{
		return;
	}

	if (IsValid(CachedUpgradeComponent))
	{
		RefreshFromUpgradeComponent(CachedUpgradeComponent);
	}
	if (!IsUpgradeRequestPending())
	{
		return;
	}
	if (!bSucceeded)
	{
		ResetUpgradeInteraction();
	}
	// 성공 응답만으로 활성화하지 않는다. 아직 상태 복제가 오지 않았다면 대기를 유지한다.
}

void UUpgradeNodeWidget::BindUpgradeContextEvents()
{
	if (IsValid(CachedUpgradeComponent))
	{
		CachedUpgradeComponent->OnUpgradeStateChanged.AddUniqueDynamic(this, &UUpgradeNodeWidget::HandleUpgradeStateChanged);
	}
	if (IsValid(CachedPlayerState))
	{
		if (!NodeCountChangedHandle.IsValid())
		{
			NodeCountChangedHandle = CachedPlayerState->OnNodeCountChanged.AddUObject(this, &UUpgradeNodeWidget::HandleNodeCountChanged);
		}
		if (!ActivatedNodesChangedHandle.IsValid())
		{
			ActivatedNodesChangedHandle = CachedPlayerState->OnActivatedUpgradeNodesChanged.AddUObject(
				this, &UUpgradeNodeWidget::HandleUpgradeStateChanged);
		}
	}
}

void UUpgradeNodeWidget::UnbindUpgradeContextEvents()
{
	if (IsValid(CachedUpgradeComponent))
	{
		CachedUpgradeComponent->OnUpgradeStateChanged.RemoveDynamic(this, &UUpgradeNodeWidget::HandleUpgradeStateChanged);
	}
	if (IsValid(CachedPlayerState))
	{
		CachedPlayerState->OnNodeCountChanged.Remove(NodeCountChangedHandle);
		CachedPlayerState->OnActivatedUpgradeNodesChanged.Remove(ActivatedNodesChangedHandle);
	}
	NodeCountChangedHandle.Reset();
	ActivatedNodesChangedHandle.Reset();
}

void UUpgradeNodeWidget::HandleUpgradeStateChanged()
{
	if (IsValid(CachedUpgradeComponent))
	{
		RefreshFromUpgradeComponent(CachedUpgradeComponent);
	}
}

void UUpgradeNodeWidget::HandleNodeCountChanged(int32 NewNodeCount)
{
	HandleUpgradeStateChanged();
}

void UUpgradeNodeWidget::HandleApplicationActivationChanged(bool bIsActive)
{
	if (!bIsActive)
	{
		SetNodeHovered(false);
		CancelHold();
	}
}

void UUpgradeNodeWidget::CacheInteractionVisibility()
{
	InteractionVisibilityWidgets.Reset();
	// ��ø UserWidget�� ��Ʈ���� �� ���� �����Ѵ�. ���� ������ �˻� ���� ǥ��/Ȱ�� ���θ� Ȯ���Ѵ�.
	UWidget* Widget = this;
	while (Widget && !InteractionVisibilityWidgets.Contains(Widget))
	{
		InteractionVisibilityWidgets.Add(Widget);
		Widget = Widget->GetParent() ? static_cast<UWidget*>(Widget->GetParent()) : Widget->GetTypedOuter<UUserWidget>();
	}
}

bool UUpgradeNodeWidget::IsInteractionVisible() const
{
	for (const TWeakObjectPtr<UWidget>& Widget : InteractionVisibilityWidgets)
	{
		if (!Widget.IsValid() || !Widget->IsVisible() || !Widget->GetIsEnabled())
		{
			return false;
		}
	}
	return !InteractionVisibilityWidgets.IsEmpty();
}

void UUpgradeNodeWidget::StartInteractionTicker()
{
	// 월드 일시정지/Time Dilation과 무관한 UI 시간이다. 누르기나 응답 대기가 끝나면 즉시 해제한다.
	if (!InteractionTickerHandle.IsValid())
	{
		LastInteractionTime = FPlatformTime::Seconds();
		InteractionTickerHandle = FTSTicker::GetCoreTicker().AddTicker(
			FTickerDelegate::CreateUObject(this, &UUpgradeNodeWidget::TickUpgradeInteraction), 1.0f / 60.0f);
	}
	if (!ApplicationActivationHandle.IsValid() && FSlateApplication::IsInitialized())
	{
		ApplicationActivationHandle = FSlateApplication::Get().OnApplicationActivationStateChanged().AddUObject(
			this, &UUpgradeNodeWidget::HandleApplicationActivationChanged);
	}
}

void UUpgradeNodeWidget::StopInteractionTicker()
{
	FTSTicker::GetCoreTicker().RemoveTicker(InteractionTickerHandle);
	InteractionTickerHandle.Reset();
	if (FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().OnApplicationActivationStateChanged().Remove(ApplicationActivationHandle);
	}
	ApplicationActivationHandle.Reset();
}

bool UUpgradeNodeWidget::TickUpgradeInteraction(float DeltaSeconds)
{
	const double Now = FPlatformTime::Seconds();
	const float Elapsed = static_cast<float>(Now - LastInteractionTime);
	LastInteractionTime = Now;
	if (!IsInteractionVisible())
	{
		SetNodeHovered(false);
		ResetUpgradeInteraction();
		return false;
	}

	if (bHoldingUpgrade)
	{
		const TSharedPtr<SWidget> ButtonWidget = NodeButton ? NodeButton->GetCachedWidget() : nullptr;
		if (!NodeButton || !NodeButton->IsPressed() || !NodeButton->IsHovered()
			|| !ButtonWidget.IsValid() || !ButtonWidget->HasMouseCapture()
			|| !FSlateApplication::IsInitialized() || !FSlateApplication::Get().IsActive())
		{
			CancelHold();
			return false;
		}
		if (AdvanceHold(Elapsed))
		{
			RequestUpgrade();
		}
	}

	CheckRequestTimeout(Now);
	if (!bHoldingUpgrade && !IsUpgradeRequestPending())
	{
		StopInteractionTicker();
		return false;
	}
	return true;
}

void UUpgradeNodeWidget::CheckRequestTimeout(double Now)
{
	const float Timeout = FMath::IsFinite(RequestTimeout) ? FMath::Max(0.01f, RequestTimeout) : 5.0f;
	if (IsUpgradeRequestPending()
		&& (!PendingUpgradeOwner.IsValid() || !UpgradeController.IsValid() || Now - RequestStartedTime >= Timeout))
	{
		// 시간 초과는 구매 취소가 아니다. 지금 상태를 반영하고 로컬 대기만 해제한다.
		HandleUpgradeStateChanged();
		ResetUpgradeInteraction();
	}
}

void UUpgradeNodeWidget::NativeOnMouseCaptureLost(const FCaptureLostEvent& CaptureLostEvent)
{
	CancelHold();
	Super::NativeOnMouseCaptureLost(CaptureLostEvent);
}

void UUpgradeNodeWidget::BeginDestroy()
{
	StopUnlockPlayback();
	StopInteractionTicker();
	ClearPendingRequest();
	UnbindUpgradeContextEvents();
	Super::BeginDestroy();
}

void UUpgradeNodeWidget::HandleHovered()
{
	// ����â�� ���ų� ���� ���� ������ ���� ���¿��� ��� ��ü�� ȣ�� ������ �����Ѵ�.
	SetNodeHovered(true);
	if (const UUpgradeNodeGroupWidget* NodeGroupWidget = FindOwningNodeGroupWidget())
	{
		NodeGroupWidget->DismissOtherNodeDescriptions(this);
	}
	if (bSuppressDescUntilUnhover)
	{
		return;
	}

	if (UOutlierUpgradeComponent* UpgradeComponent = ResolveUpgradeComponent())
	{
		RefreshFromUpgradeComponent(UpgradeComponent);
	}

	OnUpgradeNodeHovered.Broadcast(this, CurrentNodeRowName);
	// 부모 숨김으로 설명창만 정리된 경우, 다음 호버를 새 열기로 처리한다.
	if (bHoverDescVisible && CachedUpgradeDescWidget && !CachedUpgradeDescWidget->IsVisible())
	{
		bHoverDescVisible = false;
	}
	if (bHoverDescVisible)
	{
		return;
	}

	if (!ShouldShowDescOnHover())
	{
		HideUpgradeDescWidget();
		bHoverDescVisible = false;
		return;
	}

	UUpgradeDescWidget* DescWidget = EnsureUpgradeDescWidget();
	UCanvasPanel* DescCanvas = FindDescCanvas();
	if (!DescWidget || !DescCanvas)
	{
		bHoverDescVisible = false;
		return;
	}

	DescWidget->ShowNodeData(
		CurrentNodeRowName,
		CurrentNodeData,
		GetDisplayedDescState(),
		CurrentNodeCount,
		bCanAfford,
		PreviewMedia);

	FVector2D PopupPosition = FVector2D::ZeroVector;
	FVector2D PopupSize = FVector2D::ZeroVector;
	float PopupRenderScale = 1.0f;
	if (CalculateDescWidgetLayout(DescCanvas, PopupPosition, PopupSize, PopupRenderScale))
	{
		if (UCanvasPanelSlot* CanvasSlot = Cast<UCanvasPanelSlot>(DescWidget->Slot))
		{
			CanvasSlot->SetAutoSize(false);
			CanvasSlot->SetAlignment(FVector2D::ZeroVector);
			CanvasSlot->SetAnchors(FAnchors(0.0f, 0.0f));
			CanvasSlot->SetPosition(PopupPosition);
			CanvasSlot->SetSize(PopupSize);
			CanvasSlot->SetZOrder(DescPopupZOrder);
		}

		DescWidget->SetRenderTransformPivot(FVector2D::ZeroVector);
		DescWidget->SetRenderScale(FVector2D(PopupRenderScale));
	}

	bHoverDescVisible = true;
}

void UUpgradeNodeWidget::HandleUnhovered()
{
	SetNodeHovered(false);
	CancelHold();
	bSuppressDescUntilUnhover = false;
	if (bHoverDescVisible)
	{
		// 닫힘 연출은 기존 경로로 재생하고, 재진입은 새 열기로 처리한다.
		bHoverDescVisible = false;
		HideUpgradeDescWidget();
	}

	OnUpgradeNodeUnhovered.Broadcast(this, CurrentNodeRowName);
}

void UUpgradeNodeWidget::BindButtonEvents()
{
	if (!NodeButton)
	{
		return;
	}

	NodeButton->OnPressed.AddUniqueDynamic(this, &UUpgradeNodeWidget::HandlePressed);
	NodeButton->OnReleased.AddUniqueDynamic(this, &UUpgradeNodeWidget::HandleReleased);
	NodeButton->SetClickMethod(EButtonClickMethod::DownAndUp);
	NodeButton->OnHovered.AddUniqueDynamic(this, &UUpgradeNodeWidget::HandleHovered);
	NodeButton->OnUnhovered.AddUniqueDynamic(this, &UUpgradeNodeWidget::HandleUnhovered);
}

UOutlierUpgradeComponent* UUpgradeNodeWidget::ResolveUpgradeComponent()
{
	if (CachedUpgradeComponent)
	{
		return CachedUpgradeComponent;
	}

	APawn* OwningPawn = GetOwningPlayerPawn();
	if (!OwningPawn)
	{
		return nullptr;
	}

	SetUpgradeComponent(OwningPawn->FindComponentByClass<UOutlierUpgradeComponent>());
	return CachedUpgradeComponent;
}

FName UUpgradeNodeWidget::GetNodeLookupName() const
{
	FName ResolvedRowName = NAME_None;
	if (TryResolveNodeRowNameByTreeAndNodeId(TreeId, NodeId, ResolvedRowName))
	{
		return ResolvedRowName;
	}

	if (!NodeRowName.IsNone())
	{
		return NodeRowName;
	}

	if (!CurrentNodeRowName.IsNone())
	{
		return CurrentNodeRowName;
	}

	return NodeId;
}

bool UUpgradeNodeWidget::TryResolveNodeRowNameByTreeAndNodeId(FName InTreeId, FName InNodeId, FName& OutRowName) const
{
	OutRowName = NAME_None;
	if (InTreeId.IsNone() || InNodeId.IsNone())
	{
		return false;
	}

	if (CachedUpgradeComponent
		&& CachedUpgradeComponent->ResolveNodeRowNameByTreeAndNodeId(InTreeId, InNodeId, OutRowName))
	{
		return true;
	}

	const UDataTable* DataTable = FindOwningUpgradeDataTable();
	if (!DataTable)
	{
		return false;
	}

	const UUpgradeNodeGroupWidget* NodeGroupWidget = FindOwningNodeGroupWidget();
	for (const FName& RowName : DataTable->GetRowNames())
	{
		const FOutlierUpgradeNodeRow* Row = DataTable->FindRow<FOutlierUpgradeNodeRow>(RowName, TEXT("UpgradeNodeWidgetResolveRowName"), false);
		if (!Row || Row->TreeId != InTreeId || Row->NodeId != InNodeId)
		{
			continue;
		}

		if (NodeGroupWidget
			&& NodeGroupWidget->GetResolvedUpgradeRole() != EOutlierUpgradeRole::None
			&& Row->Role != NodeGroupWidget->GetResolvedUpgradeRole())
		{
			continue;
		}

		OutRowName = RowName;
		return true;
	}

	return false;
}

const UDataTable* UUpgradeNodeWidget::FindOwningUpgradeDataTable() const
{
	if (const UUpgradeNodeGroupWidget* NodeGroupWidget = FindOwningNodeGroupWidget())
	{
		return NodeGroupWidget->GetResolvedUpgradeDataTable();
	}

	return nullptr;
}

const UUpgradeNodeGroupWidget* UUpgradeNodeWidget::FindOwningNodeGroupWidget() const
{
	if (const UUpgradeNodeGroupWidget* NodeGroupWidget = GetTypedOuter<UUpgradeNodeGroupWidget>())
	{
		return NodeGroupWidget;
	}

	const UWidget* CurrentWidget = this;
	while (CurrentWidget)
	{
		const UPanelWidget* ParentWidget = CurrentWidget->GetParent();
		if (!ParentWidget)
		{
			return nullptr;
		}

		if (const UUpgradeNodeGroupWidget* NodeGroupWidget = ParentWidget->GetTypedOuter<UUpgradeNodeGroupWidget>())
		{
			return NodeGroupWidget;
		}

		CurrentWidget = ParentWidget;
	}

	return nullptr;
}

bool UUpgradeNodeWidget::ShouldShowDescOnHover() const
{
	return !CurrentNodeRowName.IsNone() || !CurrentNodeData.NodeId.IsNone();
}

EOutlierUpgradeNodeState UUpgradeNodeWidget::GetDisplayedDescState() const
{
	return bAlwaysShowUnlockedIcon ? EOutlierUpgradeNodeState::Activated : CurrentState;
}

UUpgradeDescWidget* UUpgradeNodeWidget::EnsureUpgradeDescWidget()
{
	UCanvasPanel* ParentCanvas = FindDescCanvas();
	if (!ParentCanvas)
	{
		UE_LOG(LogTemp, Warning, TEXT("[UpgradeNodeWidget] Desc popup canvas not found. Node=%s"), *GetNodeLookupName().ToString());
		return nullptr;
	}

	if (!CachedUpgradeDescWidget)
	{
		if (!UpgradeDescWidgetClass)
		{
			UE_LOG(LogTemp, Warning, TEXT("[UpgradeNodeWidget] UpgradeDescWidgetClass is not set. Node=%s"), *GetNodeLookupName().ToString());
			return nullptr;
		}

		CachedUpgradeDescWidget = CreateWidget<UUpgradeDescWidget>(GetOwningPlayer(), UpgradeDescWidgetClass);
		if (!CachedUpgradeDescWidget)
		{
			UE_LOG(LogTemp, Warning, TEXT("[UpgradeNodeWidget] Failed to create upgrade desc widget. Node=%s Class=%s"),
				*GetNodeLookupName().ToString(),
				*GetNameSafe(UpgradeDescWidgetClass.Get()));
			return nullptr;
		}
	}

	if (CachedUpgradeDescWidget->GetParent() != ParentCanvas)
	{
		CachedUpgradeDescWidget->RemoveFromParent();
		ParentCanvas->AddChildToCanvas(CachedUpgradeDescWidget);
	}
	return CachedUpgradeDescWidget;
}

void UUpgradeNodeWidget::HideUpgradeDescWidget()
{
	if (CachedUpgradeDescWidget)
	{
		CachedUpgradeDescWidget->HideNodeData(CurrentNodeRowName);
	}
}

UCanvasPanel* UUpgradeNodeWidget::FindDescCanvas() const
{
	const UWidget* CurrentWidget = this;
	while (CurrentWidget)
	{
		if (UCanvasPanel* CanvasPanel = Cast<UCanvasPanel>(CurrentWidget->GetParent()))
		{
			return CanvasPanel;
		}

		CurrentWidget = CurrentWidget->GetParent();
	}

	CurrentWidget = NodeButton;
	while (CurrentWidget)
	{
		if (UCanvasPanel* CanvasPanel = Cast<UCanvasPanel>(CurrentWidget->GetParent()))
		{
			return CanvasPanel;
		}

		CurrentWidget = CurrentWidget->GetParent();
	}

	return nullptr;
}

bool UUpgradeNodeWidget::CalculateDescWidgetLayout(
	UCanvasPanel* ParentCanvas,
	FVector2D& OutPosition,
	FVector2D& OutSize,
	float& OutRenderScale) const
{
	if (!NodeButton || !CachedUpgradeDescWidget || !ParentCanvas)
	{
		return false;
	}

	ParentCanvas->ForceLayoutPrepass();
	CachedUpgradeDescWidget->ForceLayoutPrepass();

	const FGeometry& CanvasGeometry = ParentCanvas->GetCachedGeometry();
	const FGeometry& ButtonGeometry = NodeButton->GetCachedGeometry();
	const FVector2D CanvasSize = CanvasGeometry.GetLocalSize();
	if (CanvasSize.X <= KINDA_SMALL_NUMBER || CanvasSize.Y <= KINDA_SMALL_NUMBER)
	{
		return false;
	}

	OutRenderScale = CalculateDescViewportScale(CanvasSize);

	const FVector2D ButtonTopLeft = CanvasGeometry.AbsoluteToLocal(ButtonGeometry.LocalToAbsolute(FVector2D::ZeroVector));
	const FVector2D ButtonBottomRight = CanvasGeometry.AbsoluteToLocal(ButtonGeometry.LocalToAbsolute(ButtonGeometry.GetLocalSize()));
	const FVector2D ButtonSize = ButtonBottomRight - ButtonTopLeft;
	const FVector2D ButtonCenter = ButtonTopLeft + ButtonSize * 0.5f;

	FVector2D DesiredSize = CachedUpgradeDescWidget->GetPopupDesignSize();
	if (DesiredSize.X <= KINDA_SMALL_NUMBER || DesiredSize.Y <= KINDA_SMALL_NUMBER)
	{
		DesiredSize = CachedUpgradeDescWidget->GetDesiredSize();
	}
	const FVector2D SafeFallbackSize(
		FMath::Max(DescFallbackSize.X, 1.0f),
		FMath::Max(DescFallbackSize.Y, 1.0f));
	const FVector2D SafePadding(
		FMath::Max(DescViewportPadding.X, 0.0f),
		FMath::Max(DescViewportPadding.Y, 0.0f));

	const FVector2D InnerCanvasSize(
		FMath::Max(CanvasSize.X - SafePadding.X * 2.0f, 1.0f),
		FMath::Max(CanvasSize.Y - SafePadding.Y * 2.0f, 1.0f));

	if (DesiredSize.X <= KINDA_SMALL_NUMBER || DesiredSize.Y <= KINDA_SMALL_NUMBER)
	{
		DesiredSize = SafeFallbackSize;
	}

	OutSize.X = FMath::Min(DesiredSize.X, InnerCanvasSize.X / OutRenderScale);
	OutSize.Y = FMath::Min(DesiredSize.Y, InnerCanvasSize.Y / OutRenderScale);
	const FVector2D RenderedSize = OutSize * OutRenderScale;
	const FVector2D ScaledOffset = DescPopupOffset * OutRenderScale;
	const float Gap = FMath::Max(ScaledOffset.X, 0.0f);
	const float CenteredY = ButtonCenter.Y - RenderedSize.Y * 0.5f + ScaledOffset.Y;
	// ����� Ʈ���� ����â�� �� �������� ��ȯ���� �ʰ� �׻� ������ ���ϰ� �Ѵ�.
	const float PopupX = ButtonCenter.X < CanvasSize.X * 0.5f
		? ButtonBottomRight.X + Gap
		: ButtonTopLeft.X - Gap - RenderedSize.X;

	const FVector2D MaxPosition(
		FMath::Max(SafePadding.X, CanvasSize.X - SafePadding.X - RenderedSize.X),
		FMath::Max(SafePadding.Y, CanvasSize.Y - SafePadding.Y - RenderedSize.Y));

	OutPosition.X = FMath::Clamp(PopupX, SafePadding.X, MaxPosition.X);
	OutPosition.Y = FMath::Clamp(CenteredY, SafePadding.Y, MaxPosition.Y);
	return true;
}

float UUpgradeNodeWidget::CalculateDescViewportScale(const FVector2D& CanvasSize) const
{
	const FVector2D SafeBaseResolution(
		FMath::Max(DescBaseDesignResolution.X, 1.0f),
		FMath::Max(DescBaseDesignResolution.Y, 1.0f));
	const float RawScale = FMath::Min(CanvasSize.X / SafeBaseResolution.X, CanvasSize.Y / SafeBaseResolution.Y);
	const float MinScale = FMath::Max(DescMinViewportScale, KINDA_SMALL_NUMBER);
	const float MaxScale = FMath::Max(DescMaxViewportScale, MinScale);
	return FMath::Clamp(RawScale, MinScale, MaxScale);
}

void UUpgradeNodeWidget::ApplyInjectedPlayerState(FOutlierUpgradeNodeViewData& InOutViewData) const
{
	if (!CachedPlayerState)
	{
		return;
	}

	InOutViewData.CurrentNodeCount = CachedPlayerState->GetNodeCount();
	InOutViewData.bCanAfford = InOutViewData.CurrentNodeCount >= InOutViewData.NodeRow.Cost;

	// ���ἱ�� ���� ���� ����� ����Ѵ�. ������Ʈ�� ������ �ʾ �ڹ��� ��⸦ ���� �����ؾ� �Ѵ�.
	const TArray<FName>& ActivatedNodes = CachedPlayerState->GetActivatedUpgradeNodeIds(InOutViewData.NodeRow.Role);
	if (InOutViewData.NodeRow.bInitiallyActivated || ActivatedNodes.Contains(InOutViewData.RowName))
	{
		InOutViewData.State = EOutlierUpgradeNodeState::Activated;
		return;
	}

	bool bParentActivated = InOutViewData.NodeRow.ParentId.IsNone();
	if (!bParentActivated && CachedUpgradeComponent)
	{
		FOutlierUpgradeNodeViewData Parent;
		if (CachedUpgradeComponent->GetNodeViewData(InOutViewData.NodeRow.ParentId, Parent))
		{
			bParentActivated = Parent.NodeRow.bInitiallyActivated || ActivatedNodes.Contains(Parent.RowName);
		}
	}
	InOutViewData.State = bParentActivated ? EOutlierUpgradeNodeState::Unlocked : EOutlierUpgradeNodeState::Locked;
}

void UUpgradeNodeWidget::RefreshVisibleDescWidget()
{
	if (!bHoverDescVisible || !CachedUpgradeDescWidget)
	{
		return;
	}

	if (!ShouldShowDescOnHover())
	{
		HideUpgradeDescWidget();
		bHoverDescVisible = false;
		return;
	}

	CachedUpgradeDescWidget->UpdateNodeData(
		CurrentNodeRowName,
		CurrentNodeData,
		GetDisplayedDescState(),
		CurrentNodeCount,
		bCanAfford);
}

TArray<FPropertyTextFName> UUpgradeNodeWidget::GetNodeRowNameOptions() const
{
	TArray<FPropertyTextFName> Options;
	const UDataTable* DataTable = FindOwningUpgradeDataTable();
	const UUpgradeNodeGroupWidget* NodeGroupWidget = FindOwningNodeGroupWidget();
	if (!DataTable)
	{
		return Options;
	}

	TArray<FName> RowNames = DataTable->GetRowNames();
	RowNames.Sort(FNameLexicalLess());

	for (const FName& RowName : RowNames)
	{
		const FOutlierUpgradeNodeRow* Row = DataTable->FindRow<FOutlierUpgradeNodeRow>(RowName, TEXT("UpgradeNodeWidgetOptions"), false);
		if (!Row)
		{
			continue;
		}

		if (NodeGroupWidget
			&& NodeGroupWidget->GetResolvedUpgradeRole() != EOutlierUpgradeRole::None
			&& Row->Role != NodeGroupWidget->GetResolvedUpgradeRole())
		{
			continue;
		}

		const bool bIsCurrentSelection =
			RowName == NodeRowName
			|| (Row->TreeId == TreeId && Row->NodeId == NodeId);
		if (!bIsCurrentSelection
			&& NodeGroupWidget
			&& NodeGroupWidget->IsNodeIdSelectedByOtherWidget(this, Row->TreeId, Row->NodeId))
		{
			continue;
		}

		FPropertyTextFName Option;
		Option.ValueString = RowName;
		Option.DisplayName = BuildNodeRowOptionDisplayName(RowName, *Row);
		Options.Add(Option);
	}

	return Options;
}

TArray<FPropertyTextFName> UUpgradeNodeWidget::GetTreeIdOptions() const
{
	TArray<FPropertyTextFName> Options;
	const UDataTable* DataTable = FindOwningUpgradeDataTable();
	const UUpgradeNodeGroupWidget* NodeGroupWidget = FindOwningNodeGroupWidget();
	if (!DataTable)
	{
		return Options;
	}

	TArray<FName> TreeIds;
	for (const FName& RowName : DataTable->GetRowNames())
	{
		const FOutlierUpgradeNodeRow* Row = DataTable->FindRow<FOutlierUpgradeNodeRow>(RowName, TEXT("UpgradeNodeWidgetTreeOptions"), false);
		if (!Row || Row->TreeId.IsNone())
		{
			continue;
		}

		if (NodeGroupWidget
			&& NodeGroupWidget->GetResolvedUpgradeRole() != EOutlierUpgradeRole::None
			&& Row->Role != NodeGroupWidget->GetResolvedUpgradeRole())
		{
			continue;
		}

		TreeIds.AddUnique(Row->TreeId);
	}

	TreeIds.Sort(FNameLexicalLess());
	for (const FName& OptionName : TreeIds)
	{
		FPropertyTextFName Option;
		Option.ValueString = OptionName;
		Option.DisplayName = FText::FromName(OptionName);
		Options.Add(Option);
	}

	return Options;
}

TArray<FPropertyTextFName> UUpgradeNodeWidget::GetNodeIdOptions() const
{
	TArray<FPropertyTextFName> Options;
	const UDataTable* DataTable = FindOwningUpgradeDataTable();
	const UUpgradeNodeGroupWidget* NodeGroupWidget = FindOwningNodeGroupWidget();
	if (!DataTable || TreeId.IsNone())
	{
		return Options;
	}

	TArray<FName> RowNames = DataTable->GetRowNames();
	RowNames.Sort(FNameLexicalLess());

	for (const FName& RowName : RowNames)
	{
		const FOutlierUpgradeNodeRow* Row = DataTable->FindRow<FOutlierUpgradeNodeRow>(RowName, TEXT("UpgradeNodeWidgetNodeOptions"), false);
		if (!Row || Row->NodeId.IsNone() || Row->TreeId != TreeId)
		{
			continue;
		}

		if (NodeGroupWidget
			&& NodeGroupWidget->GetResolvedUpgradeRole() != EOutlierUpgradeRole::None
			&& Row->Role != NodeGroupWidget->GetResolvedUpgradeRole())
		{
			continue;
		}

		const bool bIsCurrentSelection = Row->NodeId == NodeId;
		if (!bIsCurrentSelection
			&& NodeGroupWidget
			&& NodeGroupWidget->IsNodeIdSelectedByOtherWidget(this, Row->TreeId, Row->NodeId))
		{
			continue;
		}

		FString DisplayString = Row->NodeId.ToString();
		if (!Row->DisplayName.IsEmpty())
		{
			DisplayString += FString::Printf(TEXT(" - %s"), *Row->DisplayName.ToString());
		}
		DisplayString += FString::Printf(TEXT(" (%s)"), *RowName.ToString());

		FPropertyTextFName Option;
		Option.ValueString = Row->NodeId;
		Option.DisplayName = FText::FromString(DisplayString);
		Options.Add(Option);
	}

	return Options;
}

FText UUpgradeNodeWidget::BuildNodeRowOptionDisplayName(FName RowName, const FOutlierUpgradeNodeRow& NodeRow)
{
	FString DisplayString = FString::Printf(
		TEXT("%s / %s"),
		*NodeRow.TreeId.ToString(),
		*NodeRow.NodeId.ToString());

	if (!NodeRow.DisplayName.IsEmpty())
	{
		DisplayString += FString::Printf(TEXT(" - %s"), *NodeRow.DisplayName.ToString());
	}

	DisplayString += FString::Printf(TEXT(" (%s)"), *RowName.ToString());
	return FText::FromString(DisplayString);
}

void UUpgradeNodeWidget::RefreshEnabledState()
{
	if (NodeButton)
	{
		NodeButton->SetIsEnabled(true);
	}
}

UMaterialInstanceDynamic* UUpgradeNodeWidget::EnsureOverlayMaterial(UImage* Image, UMaterialInstanceDynamic* CachedMaterial) const
{
	if (!Image)
	{
		return nullptr;
	}

	UObject* BrushResource = Image->GetBrush().GetResourceObject();
	if (CachedMaterial && BrushResource == CachedMaterial)
	{
		return CachedMaterial;
	}

	UTexture* SourceTexture = Cast<UTexture>(BrushResource);
	UMaterialInterface* BrushMaterial = Cast<UMaterialInterface>(BrushResource);
	if (BrushMaterial && !SourceTexture)
	{
		BrushMaterial->GetTextureParameterValue(FHashedMaterialParameterInfo(TextureParameterName), SourceTexture);
		if (!SourceTexture)
		{
			BrushMaterial->GetTextureParameterValue(FHashedMaterialParameterInfo(NodeTextureParameterName), SourceTexture);
		}
	}
	if (NodeColorMaterial && BrushMaterial != NodeColorMaterial.Get()
		&& (!BrushMaterial || SourceTexture || Image == Icon.Get()))
	{
		Image->SetBrushFromMaterial(NodeColorMaterial);
	}

	UMaterialInstanceDynamic* DynamicMaterial = Image->GetDynamicMaterial();
	if (DynamicMaterial && SourceTexture)
	{
		DynamicMaterial->SetTextureParameterValue(TextureParameterName, SourceTexture);
	}
	return DynamicMaterial;
}

UMaterialInstanceDynamic* UUpgradeNodeWidget::EnsureNodeFillMaterial(UImage* Image, UMaterialInstanceDynamic* CachedMaterial, UTexture2D* TextureOverride)
{
	if (!Image || !NodeFillMaterial)
	{
		return nullptr;
	}

	UObject* BrushResource = Image->GetBrush().GetResourceObject();
	UTexture* SourceTexture = TextureOverride ? TextureOverride : Cast<UTexture>(BrushResource);
	if (!SourceTexture)
	{
		if (UMaterialInterface* BrushMaterial = Cast<UMaterialInterface>(BrushResource))
		{
			// ���� ����� �귯���� �ؽ�ó�� �̾�޾� �������� ��Ƽ������ �⺻ ������� �ٲ��� �ʰ� �Ѵ�.
			BrushMaterial->GetTextureParameterValue(FHashedMaterialParameterInfo(NodeTextureParameterName), SourceTexture);
			if (!SourceTexture)
			{
				BrushMaterial->GetTextureParameterValue(FHashedMaterialParameterInfo(TextureParameterName), SourceTexture);
			}
		}
	}
	if (!SourceTexture)
	{
		// �̹����� ���� ���̾ ��Ƽ������ �⺻ ��� �̹����� ���Ƿ� ǥ������ �ʴ´�.
		return nullptr;
	}

	UMaterialInstanceDynamic* DynamicMaterial = CachedMaterial;
	if (!DynamicMaterial || BrushResource != DynamicMaterial || CachedNodeFillSource != NodeFillMaterial)
	{
		// �̹������� MID�� ���� �����. ���� MID�� �����Ǿ �θ𿡼� ������ ������ ���´�.
		UMaterialInstanceDynamic* SourceDynamic = Cast<UMaterialInstanceDynamic>(NodeFillMaterial.Get());
		DynamicMaterial = UMaterialInstanceDynamic::Create(SourceDynamic ? SourceDynamic->Parent.Get() : NodeFillMaterial.Get(), this);
		if (SourceDynamic && DynamicMaterial)
		{
			DynamicMaterial->CopyParameterOverrides(SourceDynamic);
		}
		Image->SetBrushFromMaterial(DynamicMaterial);
	}
	if (DynamicMaterial)
	{
		DynamicMaterial->SetTextureParameterValue(NodeTextureParameterName, SourceTexture);
	}
	return DynamicMaterial;
}

void UUpgradeNodeWidget::RefreshNodeAppearance()
{
	const bool bActivated = CurrentState == EOutlierUpgradeNodeState::Activated && !bUnlockPlaying;
	const bool bHighlight = bNodeHovered && !bActivated;
	// ȣ�� ������ �ܰ����� Glow���� �����Ѵ�. �������� ���� ���� ������ �����Ѵ�.
	if (Frame && bFrameHoverTintApplied && (!bHighlight || bUseMaterialStateColors || NodeFillMaterial))
	{
		Frame->SetColorAndOpacity(FrameBaseTint);
		bFrameHoverTintApplied = false;
	}
	if (NodeFillMaterial)
	{
		BackgroundMaterial = EnsureNodeFillMaterial(Background, BackgroundMaterial, NodeFillTexture);
		FrameMaterial = EnsureNodeFillMaterial(Frame, FrameMaterial);
		IconMaterial = EnsureNodeFillMaterial(Icon, IconMaterial);
		CachedNodeFillSource = NodeFillMaterial;
	}
	else if (CachedNodeFillSource)
	{
		// ������ �����ϸ� �ؽ�ó�� �̾�޾� ���� ����� ��Ƽ���� �������� �ǵ�����.
		for (UImage* Image : { Background.Get(), Frame.Get(), Icon.Get() })
		{
			if (Image)
			{
				if (UMaterialInterface* Material = Cast<UMaterialInterface>(Image->GetBrush().GetResourceObject()))
				{
					UTexture* Texture = nullptr;
					if (Material->GetTextureParameterValue(FHashedMaterialParameterInfo(NodeTextureParameterName), Texture))
					{
						Image->SetBrushFromTexture(Cast<UTexture2D>(Texture));
					}
				}
			}
		}
		BackgroundMaterial = nullptr;
		FrameMaterial = nullptr;
		IconMaterial = nullptr;
		CachedNodeFillSource = nullptr;
	}
	if (bUseMaterialStateColors || NodeFillMaterial)
	{
		if (!NodeFillMaterial)
		{
			FrameMaterial = EnsureOverlayMaterial(Frame, FrameMaterial);
			IconMaterial = EnsureOverlayMaterial(Icon, IconMaterial);
		}
		GlowMaterial = EnsureOverlayMaterial(Glow, GlowMaterial);

		FLinearColor StateColor = CurrentState == EOutlierUpgradeNodeState::Locked ? LockedColor : UnlockedColor;
		if (bActivated)
		{
			StateColor = ActivatedColor;
		}

		for (UMaterialInstanceDynamic* Material : { FrameMaterial.Get(), GlowMaterial.Get(), IconMaterial.Get() })
		{
			if (Material && (!NodeFillMaterial || Material == GlowMaterial.Get()))
			{
				Material->SetVectorParameterValue(ColorParameterName, StateColor);
				Material->SetScalarParameterValue(IntensityParameterName, OverlayIntensity);
			}
		}
		if (bHighlight)
		{
			for (UMaterialInstanceDynamic* Material : { FrameMaterial.Get(), GlowMaterial.Get() })
			{
				if (Material && (!NodeFillMaterial || Material == GlowMaterial.Get()))
				{
					Material->SetVectorParameterValue(ColorParameterName, HoverColor);
				}
			}
		}
	}

	if (Frame && bHighlight && ((!bUseMaterialStateColors && !NodeFillMaterial) || !FrameMaterial))
	{
		if (!bFrameHoverTintApplied)
		{
			FrameBaseTint = Frame->GetColorAndOpacity();
			bFrameHoverTintApplied = true;
		}
		Frame->SetColorAndOpacity(FrameBaseTint * HoverColor);
	}
	for (UImage* Image : { Background.Get(), Frame.Get(), Glow.Get(), Icon.Get() })
	{
		if (Image && Image->IsVisible())
		{
			Image->SetVisibility(ESlateVisibility::HitTestInvisible);
		}
	}
	if (Glow)
	{
		const bool bShowGlow = !bUnlockPlaying && (bActivated || bNodeHovered);
		Glow->SetRenderOpacity(bShowGlow ? 1.0f : 0.0f);
		Glow->SetVisibility(bShowGlow ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Hidden);
	}
	// ���� ���� ���� ��� ���� ���/�׵θ�/������ ��ü�� ��ȯ�ϹǷ� ���� Fill �̹����� �����.
	if (NodeFill)
	{
		NodeFill->SetRenderOpacity(0.0f);
		NodeFill->SetVisibility(ESlateVisibility::Hidden);
	}
	RefreshNodeFill();
}

bool UUpgradeNodeWidget::ShouldShowActivatedAppearance() const
{
	return !bUnlockPlaying && (CurrentState == EOutlierUpgradeNodeState::Activated || bAlwaysShowUnlockedIcon);
}

bool UUpgradeNodeWidget::EnsureUnlockMaterial()
{
	if (!bUseUnlockPresentation || bAlwaysShowUnlockedIcon) return false;
	UMaterialInterface* Material = UnlockLock
		? Cast<UMaterialInterface>(UnlockLock->GetBrush().GetResourceObject()) : nullptr;
	const bool bValidSettings = FMath::IsFinite(UnlockDuration) && UnlockDuration > 0.0f
		&& UnlockFrameCount > 0 && UnlockFrameCount <= 19;
	// ��� �߿��� �̹� ������ ���� ��Ƽ������ �����Ѵ�. �� ������ ��Ƽ���� �Ű������� �ٽ� �˻����� �ʴ´�.
	if (Material && Material == UnlockMaterial && bValidSettings) return true;
	float FrameValue = 0.0f;
	// ��Ʈ�� ��Ƽ���� �����ص� �ȴ�. �ڵ忡�� �����ϴ� ������ ���� �ʼ��� Ȯ���Ѵ�.
	const bool bValid = Material && bValidSettings
		&& Material->GetScalarParameterValue(FHashedMaterialParameterInfo(TEXT("FrameIndex")), FrameValue);
	if (!bValid)
	{
		if (!bUnlockConfigurationWarned && !IsDesignTime())
		{
			UE_LOG(LogTemp, Warning, TEXT("[Upgrade.Unlock] Invalid UnlockLock material/FrameIndex/duration: %s"), *GetName());
			bUnlockConfigurationWarned = true;
		}
		return false;
	}
	if (!UnlockMaterial || Material != UnlockMaterial)
	{
		UnlockMaterial = UMaterialInstanceDynamic::Create(Material, this);
		UnlockLock->SetBrushFromMaterial(UnlockMaterial);
	}
	return UnlockMaterial != nullptr;
}

void UUpgradeNodeWidget::RefreshUnlockImage()
{
	const bool bShowLock = bUseUnlockPresentation && !bAlwaysShowUnlockedIcon
		&& (CurrentState != EOutlierUpgradeNodeState::Activated || bUnlockPlaying) && EnsureUnlockMaterial();
	if (UnlockLock)
	{
		UnlockLock->SetVisibility(bShowLock ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
		if (bShowLock && !bUnlockPlaying) UnlockMaterial->SetScalarParameterValue(TEXT("FrameIndex"), 0.0f);
	}
	// ���� ��� �����ܿ� �׷��� �ڹ���� �� ��������Ʈ�� ��ġ�� �ʰ� �Ѵ�.
	if (Icon && bShowLock && bHideIconWhileLocked) Icon->SetVisibility(ESlateVisibility::Hidden);
}

void UUpgradeNodeWidget::StopUnlockPlayback()
{
	FTSTicker::GetCoreTicker().RemoveTicker(UnlockTickerHandle);
	UnlockTickerHandle.Reset();
	bUnlockPlaying = false;
	UnlockElapsed = 0.0f;
	UnlockVisibilityWidgets.Reset();
}

void UUpgradeNodeWidget::ResetUnlockPresentation()
{
	StopUnlockPlayback();
	bUnlockStateInitialized = false;
	UnlockRestoreRevision = 0;
}

bool UUpgradeNodeWidget::IsUnlockVisible() const
{
	for (const TWeakObjectPtr<UWidget>& Widget : UnlockVisibilityWidgets)
	{
		if (!Widget.IsValid() || !Widget->IsVisible()) return false;
	}
	return !UnlockVisibilityWidgets.IsEmpty();
}

bool UUpgradeNodeWidget::AdvanceUnlockPlayback(float DeltaSeconds)
{
	if (!bUnlockPlaying) return false;
	const bool bValid = EnsureUnlockMaterial();
	UnlockElapsed += FMath::IsFinite(DeltaSeconds) ? FMath::Max(0.0f, DeltaSeconds) : 0.0f;
	if (bValid)
	{
		const int32 FrameIndex = FMath::Min(FMath::FloorToInt(FMath::Min(UnlockElapsed / UnlockDuration, 1.0f)
			* UnlockFrameCount), UnlockFrameCount - 1);
		UnlockMaterial->SetScalarParameterValue(TEXT("FrameIndex"), static_cast<float>(FrameIndex));
	}
	if (!bValid || UnlockElapsed >= UnlockDuration)
	{
		// ������ �����ӿ� ������ ������ �ƴ϶� ǥ�� �ð��� ������ �Ϸ��Ѵ�. ���� �ð��� ���⼭ �����Ѵ�.
		StopUnlockPlayback();
		RefreshNodeAppearance();
		RefreshNodeTexture();
		OnUnlockPresentationFinished.Broadcast(this);
		return false;
	}
	return true;
}

bool UUpgradeNodeWidget::TickUnlockPlayback(float DeltaSeconds)
{
	if (!IsUnlockVisible())
	{
		// ������ ȭ�鿡�� �ϷḦ �뺸���� �ʴ´�. �ٽ� ���̸� ���� ���� ���¸� ��� ǥ���Ѵ�.
		StopUnlockPlayback();
		RefreshNodeAppearance();
		RefreshNodeTexture();
		OnUnlockPresentationCancelled.Broadcast(this);
		return false;
	}
	const double Now = FPlatformTime::Seconds();
	const float Elapsed = static_cast<float>(FMath::Max(0.0, Now - LastUnlockTime));
	LastUnlockTime = Now;
	return AdvanceUnlockPlayback(Elapsed);
}

void UUpgradeNodeWidget::SetNodeHovered(bool bInHovered)
{
	bNodeHovered = bInHovered;
	RefreshNodeAppearance();
}

void UUpgradeNodeWidget::RefreshNodeFill()
{
	if (!NodeFillMaterial)
	{
		return;
	}
	const bool bShowActivated = ShouldShowActivatedAppearance();
	const float Progress = bShowActivated ? 1.0f : FMath::Clamp(HoldProgress, 0.0f, 1.0f);
	const FLinearColor StateColor = CurrentState == EOutlierUpgradeNodeState::Locked ? LockedColor : UnlockedColor;
	for (UMaterialInstanceDynamic* Material : { BackgroundMaterial.Get(), FrameMaterial.Get(), IconMaterial.Get() })
	{
		if (!Material)
		{
			continue;
		}
		const FLinearColor BaseColor = Material == FrameMaterial.Get() && bNodeHovered && !bShowActivated ? HoverColor : StateColor;
		// 0/1������ ���� ���� ���� �� If�� ��� �ȼ��� ������ �⺻/Ȱ�� ����� �ǵ��� �Ѵ�.
		// Ȱ��ȭ �Ŀ��� �̹����� ������ �ʴ´�. ���� ���°� Progress=1�� �����ϰ� Glow�� ������ �Ҵ�.
		Material->SetVectorParameterValue(UnlockedColorParameterName, Progress >= 1.0f ? ActivatedColor : BaseColor);
		Material->SetVectorParameterValue(ActivatedColorParameterName, Progress <= 0.0f ? BaseColor : ActivatedColor);
		Material->SetScalarParameterValue(UpgradeNodeProgressParameterName, Progress);
		if (Material != IconMaterial.Get())
		{
			Material->SetScalarParameterValue(UseActivatedTextureParameterName, 0.0f);
		}
	}
}

void UUpgradeNodeWidget::RefreshNodeTexture()
{
	// � �ؽ�ó ��η� ��ȯ�ص� �������� �ڹ���/�ߺ� ������ ǥ�ø� �����.
	ON_SCOPE_EXIT{ RefreshUnlockImage(); };
	if (!Icon)
	{
		return;
	}

	UTexture2D* BaseTexture = NodeIconTextureOverride ? NodeIconTextureOverride.Get() : NodeTexture.Get();
	if (NodeFillMaterial)
	{
		UTexture2D* InactiveTexture = LockedIconTextureOverride ? LockedIconTextureOverride.Get() : BaseTexture;
		if (!InactiveTexture) InactiveTexture = DeactivatedNodeTexture.Get();
		UTexture2D* ActiveTexture = UnlockedIconTextureOverride ? UnlockedIconTextureOverride.Get() : BaseTexture;
		if (!ActiveTexture) ActiveTexture = UnlockedNodeTexture.Get();
		// ���� �̹����� �־ ǥ�ø� �����Ѵ�. �� �̹����� ���� MID�� �ְ� Progress�� ��ȯ�Ѵ�.
		if (!InactiveTexture) InactiveTexture = ActiveTexture;
		if (!ActiveTexture) ActiveTexture = InactiveTexture;
		Icon->SetVisibility(InactiveTexture ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
		if (InactiveTexture)
		{
			IconMaterial = EnsureNodeFillMaterial(Icon, IconMaterial, InactiveTexture);
			if (IconMaterial)
			{
				IconMaterial->SetTextureParameterValue(ActivatedTextureParameterName, ActiveTexture);
				IconMaterial->SetScalarParameterValue(UseActivatedTextureParameterName, ActiveTexture != InactiveTexture ? 1.0f : 0.0f);
			}
		}
		RefreshNodeFill();
		return;
	}
	UTexture2D* DisplayTexture = nullptr;
	const bool bShowUnlockedIcon = IsDesignTime() || ShouldShowActivatedAppearance();
	if (!bShowUnlockedIcon && LockedIconTextureOverride)
	{
		DisplayTexture = LockedIconTextureOverride.Get();
	}
	else if (bShowUnlockedIcon && UnlockedIconTextureOverride)
	{
		DisplayTexture = UnlockedIconTextureOverride.Get();
	}
	else if (bUseMaterialStateColors)
	{
		DisplayTexture = BaseTexture;
		if (!DisplayTexture)
		{
			DisplayTexture = !bShowUnlockedIcon
				? DeactivatedNodeTexture.Get()
				: UnlockedNodeTexture.Get();
		}
	}
	else if (!bShowUnlockedIcon)
	{
		DisplayTexture = DeactivatedNodeTexture ? DeactivatedNodeTexture.Get() : BaseTexture;
	}
	else
	{
		DisplayTexture = BaseTexture ? BaseTexture : UnlockedNodeTexture.Get();
	}

	Icon->SetVisibility(DisplayTexture ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	if (DisplayTexture)
	{
		if (bUseMaterialStateColors)
		{
			IconMaterial = EnsureOverlayMaterial(Icon, IconMaterial);
			if (IconMaterial)
			{
				IconMaterial->SetTextureParameterValue(TextureParameterName, DisplayTexture);
				return;
			}
		}
		Icon->SetBrushFromTexture(DisplayTexture);
		IconMaterial = nullptr;
	}
}
