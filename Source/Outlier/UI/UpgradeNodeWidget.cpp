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
#include "Framework/Application/SlateApplication.h"
#include "UI/UpgradeDescWidget.h"
#include "UI/UpgradeNodeGroupWidget.h"
#include "Upgrade/OutlierUpgradeComponent.h"
#include "Upgrade/OutlierUpgradeSetData.h"
#include "TimerManager.h"
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
	const FName ProgressParameterName(TEXT("Progress"));
}

void UUpgradeNodeWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();

	BindButtonEvents();
}

void UUpgradeNodeWidget::NativeConstruct()
{
	Super::NativeConstruct();

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
	SetNodeHovered(false);
	ResetUpgradeInteraction();
	UnbindUpgradeContextEvents();
	if (GetWorld())
	{
		GetWorld()->GetTimerManager().ClearTimer(DescHideTimerHandle);
	}
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

	// 같은 컴포넌트를 재사용해도 데이터 묶음이 교체되면 이전 화면의 입력/응답을 이어받지 않는다.
	if (CachedUpgradeSetData.Get() != UpgradeComponent->GetUpgradeSetData())
	{
		ResetUpgradeInteraction();
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
	if (bHoverDescVisible)
	{
		if (GetWorld())
		{
			GetWorld()->GetTimerManager().ClearTimer(DescHideTimerHandle);
		}
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
		// 성공 응답보다 상태 복제가 먼저 와도 여기서 완료한다. 이후 늦은 응답은 무시된다.
		ResetUpgradeInteraction();
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

	// 완료를 먼저 소비한 뒤 요청한다. 같은 프레임의 해제나 다음 갱신이 구매를 다시 보내지 않는다.
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
	// 중첩 UserWidget의 루트까지 한 번만 수집한다. 이후 갱신은 검색 없이 표시/활성 여부만 확인한다.
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
	StopInteractionTicker();
	ClearPendingRequest();
	UnbindUpgradeContextEvents();
	Super::BeginDestroy();
}

void UUpgradeNodeWidget::HandleHovered()
{
	// 설명창이 없거나 구매 직후 설명을 숨긴 상태여도 노드 자체의 호버 강조는 갱신한다.
	SetNodeHovered(true);
	if (const UUpgradeNodeGroupWidget* NodeGroupWidget = FindOwningNodeGroupWidget())
	{
		NodeGroupWidget->DismissOtherNodeDescriptions(this);
	}
	if (GetWorld())
	{
		GetWorld()->GetTimerManager().ClearTimer(DescHideTimerHandle);
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
			bCanAfford);

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
	ScheduleDescHide();

	OnUpgradeNodeUnhovered.Broadcast(this, CurrentNodeRowName);
}

void UUpgradeNodeWidget::ScheduleDescHide()
{
	if (!bHoverDescVisible || !GetWorld())
	{
		return;
	}

	GetWorld()->GetTimerManager().SetTimer(
		DescHideTimerHandle, this, &UUpgradeNodeWidget::HandleDescHideTimer, 0.2f, true);
}

void UUpgradeNodeWidget::HandleDescHideTimer()
{
	if ((NodeButton && NodeButton->IsHovered())
		|| (CachedUpgradeDescWidget && CachedUpgradeDescWidget->GetVisibility() != ESlateVisibility::Collapsed
			&& CachedUpgradeDescWidget->GetCachedGeometry().IsUnderLocation(FSlateApplication::Get().GetCursorPos())))
	{
		return;
	}

	HideUpgradeDescWidget();
	bHoverDescVisible = false;
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
	if (GetWorld())
	{
		GetWorld()->GetTimerManager().ClearTimer(DescHideTimerHandle);
	}
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
	// Keep radial tree popups facing inward instead of switching between four directions.
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
			// 기존 색상용 브러시의 텍스처도 이어받아 아이콘이 머티리얼의 기본 배경으로 바뀌지 않게 한다.
			BrushMaterial->GetTextureParameterValue(FHashedMaterialParameterInfo(NodeTextureParameterName), SourceTexture);
			if (!SourceTexture)
			{
				BrushMaterial->GetTextureParameterValue(FHashedMaterialParameterInfo(TextureParameterName), SourceTexture);
			}
		}
	}
	if (!SourceTexture)
	{
		// 이미지가 없는 레이어에 머티리얼의 기본 배경 이미지를 임의로 표시하지 않는다.
		return nullptr;
	}

	UMaterialInstanceDynamic* DynamicMaterial = CachedMaterial;
	if (!DynamicMaterial || BrushResource != DynamicMaterial || CachedNodeFillSource != NodeFillMaterial)
	{
		// 이미지마다 MID를 따로 만든다. 공용 MID가 지정되어도 부모에서 복제해 공유를 막는다.
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
	const bool bActivated = CurrentState == EOutlierUpgradeNodeState::Activated;
	const bool bHighlight = bNodeHovered && !bActivated;
	// 호버 강조는 외곽선과 Glow에만 적용한다. 아이콘은 기존 상태 색상을 유지한다.
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
		// 설정을 해제하면 텍스처를 이어받아 기존 색상용 머티리얼 갱신으로 되돌린다.
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

		FLinearColor StateColor = LockedColor;
		if (CurrentState == EOutlierUpgradeNodeState::Unlocked)
		{
			StateColor = UnlockedColor;
		}
		else if (CurrentState == EOutlierUpgradeNodeState::Activated)
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
		Glow->SetRenderOpacity(bActivated || bNodeHovered ? 1.0f : 0.0f);
		Glow->SetVisibility(bActivated || bNodeHovered ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Hidden);
	}
	// 별도 색상 덮개 대신 기존 배경/테두리/아이콘 자체를 전환하므로 이전 Fill 이미지는 숨긴다.
	if (NodeFill)
	{
		NodeFill->SetRenderOpacity(0.0f);
		NodeFill->SetVisibility(ESlateVisibility::Hidden);
	}
	RefreshNodeFill();
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
	const bool bShowActivated = CurrentState == EOutlierUpgradeNodeState::Activated || bAlwaysShowUnlockedIcon;
	const float Progress = bShowActivated ? 1.0f : FMath::Clamp(HoldProgress, 0.0f, 1.0f);
	const FLinearColor StateColor = CurrentState == EOutlierUpgradeNodeState::Locked ? LockedColor : UnlockedColor;
	for (UMaterialInstanceDynamic* Material : { BackgroundMaterial.Get(), FrameMaterial.Get(), IconMaterial.Get() })
	{
		if (!Material)
		{
			continue;
		}
		const FLinearColor BaseColor = Material == FrameMaterial.Get() && bNodeHovered && !bShowActivated ? HoverColor : StateColor;
		// 0/1에서는 양쪽 색을 같게 해 If의 경계 픽셀도 완전히 기본/활성 모습이 되도록 한다.
		// 활성화 후에는 이미지를 숨기지 않는다. 실제 상태가 Progress=1을 유지하고 Glow만 별도로 켠다.
		Material->SetVectorParameterValue(UnlockedColorParameterName, Progress >= 1.0f ? ActivatedColor : BaseColor);
		Material->SetVectorParameterValue(ActivatedColorParameterName, Progress <= 0.0f ? BaseColor : ActivatedColor);
		Material->SetScalarParameterValue(ProgressParameterName, Progress);
		if (Material != IconMaterial.Get())
		{
			Material->SetScalarParameterValue(UseActivatedTextureParameterName, 0.0f);
		}
	}
}

void UUpgradeNodeWidget::RefreshNodeTexture()
{
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
		// 한쪽 이미지만 있어도 표시를 유지한다. 두 이미지는 같은 MID에 넣고 Progress로 전환한다.
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
	const bool bShowUnlockedIcon = IsDesignTime() || bAlwaysShowUnlockedIcon
		|| CurrentState == EOutlierUpgradeNodeState::Activated;
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
