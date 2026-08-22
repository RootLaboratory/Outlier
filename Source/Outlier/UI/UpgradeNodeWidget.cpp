#include "UI/UpgradeNodeWidget.h"

#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Components/PanelWidget.h"
#include "Engine/DataTable.h"
#include "Engine/Texture2D.h"
#include "FirstPerson/FirstPersonPlayerController.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "OutlierPlayerState.h"
#include "UI/UpgradeDescWidget.h"
#include "UI/UpgradeNodeGroupWidget.h"
#include "Upgrade/OutlierUpgradeComponent.h"
#include "Upgrade/OutlierUpgradeSetData.h"

void UUpgradeNodeWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();

	BindButtonEvents();
	CacheDefaultNodeBrush();
}

void UUpgradeNodeWidget::NativeConstruct()
{
	Super::NativeConstruct();

	CacheDefaultNodeBrush();
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
	CacheDefaultNodeBrush();
	RefreshEnabledState();
	RefreshNodeTexture();
}

void UUpgradeNodeWidget::NativeDestruct()
{
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

	CachedUpgradeComponent = UpgradeComponent;

	const FName LookupName = GetNodeLookupName();

	FOutlierUpgradeNodeViewData ViewData;
	if (!LookupName.IsNone() && UpgradeComponent->GetNodeViewData(LookupName, ViewData))
	{
		ApplyInjectedPlayerState(ViewData);
		InjectViewData(ViewData);
	}
}

void UUpgradeNodeWidget::SetUpgradeComponent(UOutlierUpgradeComponent* InUpgradeComponent)
{
	CachedUpgradeComponent = InUpgradeComponent;
	RefreshNodeRowNameFromIds();
	RefreshFromUpgradeComponent(CachedUpgradeComponent);
}

void UUpgradeNodeWidget::SetUpgradePlayerState(AOutlierPlayerState* InPlayerState)
{
	CachedPlayerState = InPlayerState;

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

void UUpgradeNodeWidget::SetNodeState(EOutlierUpgradeNodeState InState, bool bInCanAfford)
{
	CurrentState = InState;
	bCanAfford = bInCanAfford;
	RefreshEnabledState();
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

void UUpgradeNodeWidget::HandleClicked()
{
	if (bActivateNodeOnClick && CurrentState == EOutlierUpgradeNodeState::Unlocked)
	{
		if (UOutlierUpgradeComponent* UpgradeComponent = ResolveUpgradeComponent())
		{
			const FName LookupName = GetNodeLookupName();
			bool bActivationRequested = false;
			if (!LookupName.IsNone())
			{
				const UUpgradeNodeGroupWidget* NodeGroupWidget = FindOwningNodeGroupWidget();
				UOutlierUpgradeSetData* ContextSetData = NodeGroupWidget
					? NodeGroupWidget->GetUpgradeSetData()
					: nullptr;
				UDataTable* ContextDataTable = NodeGroupWidget
					? NodeGroupWidget->GetResolvedUpgradeDataTable()
					: nullptr;
				const EOutlierUpgradeRole ContextRole = NodeGroupWidget
					? NodeGroupWidget->GetResolvedUpgradeRole()
					: EOutlierUpgradeRole::None;
				if (ContextSetData)
				{
					UpgradeComponent->SetUpgradeSetData(ContextSetData);
				}
				else if (ContextRole != EOutlierUpgradeRole::None)
				{
					UpgradeComponent->SetUpgradeRole(ContextRole);
				}
				if (!ContextSetData && ContextDataTable)
				{
					UpgradeComponent->SetUpgradeDataTable(ContextDataTable);
				}

				if (AFirstPersonPlayerController* PlayerController = Cast<AFirstPersonPlayerController>(GetOwningPlayer()))
				{
					AActor* UpgradeOwner = UpgradeComponent->GetOwner();
					if (PlayerController->HasAuthority())
					{
						bActivationRequested = UpgradeComponent->TryActivateNodeForPlayerState(
							LookupName,
							CachedPlayerState ? CachedPlayerState.Get() : PlayerController->GetPlayerState<AOutlierPlayerState>());
					}
					else if (UpgradeOwner)
					{
						PlayerController->ServerTryActivateUpgradeNode(
							UpgradeOwner,
							LookupName,
							ContextSetData);
						bActivationRequested = true;
					}
				}
				else if (!UpgradeComponent->GetOwner() || UpgradeComponent->GetOwner()->HasAuthority())
				{
					bActivationRequested = UpgradeComponent->TryActivateNodeForPlayerState(LookupName, CachedPlayerState);
				}
				else
				{
					bActivationRequested = UpgradeComponent->TryActivateNode(LookupName);
				}
			}

			if (bActivationRequested)
			{
				RefreshFromUpgradeComponent(UpgradeComponent);
			}
		}
	}

	OnUpgradeNodeClicked.Broadcast(this, CurrentNodeRowName);
}

void UUpgradeNodeWidget::HandleHovered()
{
	if (UOutlierUpgradeComponent* UpgradeComponent = ResolveUpgradeComponent())
	{
		RefreshFromUpgradeComponent(UpgradeComponent);
	}

	OnUpgradeNodeHovered.Broadcast(this, CurrentNodeRowName);

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
			CurrentState,
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
	HideUpgradeDescWidget();
	bHoverDescVisible = false;

	OnUpgradeNodeUnhovered.Broadcast(this, CurrentNodeRowName);
}

void UUpgradeNodeWidget::BindButtonEvents()
{
	if (!NodeButton)
	{
		return;
	}

	NodeButton->OnClicked.AddUniqueDynamic(this, &UUpgradeNodeWidget::HandleClicked);
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

	CachedUpgradeComponent = OwningPawn->FindComponentByClass<UOutlierUpgradeComponent>();
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

	FVector2D DesiredSize = CachedUpgradeDescWidget->GetDesiredSize();
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

	const int32 PreferredXSign = ButtonCenter.X >= CanvasSize.X * 0.5f ? -1 : 1;
	const int32 PreferredYSign = ButtonCenter.Y >= CanvasSize.Y * 0.5f ? -1 : 1;

	const auto BuildPosition = [&ButtonTopLeft, &ButtonSize, &RenderedSize, &ScaledOffset](int32 XSign, int32 YSign)
	{
		FVector2D CandidatePosition = FVector2D::ZeroVector;
		CandidatePosition.X = XSign > 0
			? ButtonTopLeft.X + ButtonSize.X + ScaledOffset.X
			: ButtonTopLeft.X - ScaledOffset.X - RenderedSize.X;
		CandidatePosition.Y = YSign > 0
			? ButtonTopLeft.Y + ButtonSize.Y + ScaledOffset.Y
			: ButtonTopLeft.Y - ScaledOffset.Y - RenderedSize.Y;
		return CandidatePosition;
	};

	const auto GetOverflow = [&CanvasSize, &SafePadding, &RenderedSize](const FVector2D& Position)
	{
		float Overflow = 0.0f;
		Overflow += FMath::Max(SafePadding.X - Position.X, 0.0f);
		Overflow += FMath::Max(SafePadding.Y - Position.Y, 0.0f);
		Overflow += FMath::Max(Position.X + RenderedSize.X - (CanvasSize.X - SafePadding.X), 0.0f);
		Overflow += FMath::Max(Position.Y + RenderedSize.Y - (CanvasSize.Y - SafePadding.Y), 0.0f);
		return Overflow;
	};

	const int32 CandidateSigns[4][2] =
	{
		{ PreferredXSign, PreferredYSign },
		{ PreferredXSign, -PreferredYSign },
		{ -PreferredXSign, PreferredYSign },
		{ -PreferredXSign, -PreferredYSign }
	};

	FVector2D BestPosition = BuildPosition(PreferredXSign, PreferredYSign);
	float BestOverflow = GetOverflow(BestPosition);
	for (const int32(&CandidateSign)[2] : CandidateSigns)
	{
		const FVector2D CandidatePosition = BuildPosition(CandidateSign[0], CandidateSign[1]);
		const float CandidateOverflow = GetOverflow(CandidatePosition);
		if (CandidateOverflow <= KINDA_SMALL_NUMBER)
		{
			OutPosition = CandidatePosition;
			return true;
		}

		if (CandidateOverflow < BestOverflow)
		{
			BestOverflow = CandidateOverflow;
			BestPosition = CandidatePosition;
		}
	}

	const FVector2D MaxPosition(
		FMath::Max(SafePadding.X, CanvasSize.X - SafePadding.X - RenderedSize.X),
		FMath::Max(SafePadding.Y, CanvasSize.Y - SafePadding.Y - RenderedSize.Y));

	OutPosition.X = FMath::Clamp(BestPosition.X, SafePadding.X, MaxPosition.X);
	OutPosition.Y = FMath::Clamp(BestPosition.Y, SafePadding.Y, MaxPosition.Y);
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
		CurrentState,
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

void UUpgradeNodeWidget::CacheDefaultNodeBrush()
{
	if (!NodeImage || bDefaultNodeBrushCached)
	{
		return;
	}

	DefaultNodeBrush = NodeImage->GetBrush();
	bDefaultNodeBrushCached = true;
}

void UUpgradeNodeWidget::RefreshNodeTexture()
{
	if (!NodeImage)
	{
		return;
	}

	CacheDefaultNodeBrush();

	if (UnlockedNodeTexture
		&& CurrentState != EOutlierUpgradeNodeState::Locked)
	{
		NodeImage->SetBrushFromTexture(UnlockedNodeTexture);
		return;
	}

	if (bDefaultNodeBrushCached)
	{
		NodeImage->SetBrush(DefaultNodeBrush);
	}
}
