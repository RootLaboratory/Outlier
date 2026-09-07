#include "UI/UpgradeNodeGroupWidget.h"

#include "Components/PanelWidget.h"
#include "Engine/DataTable.h"
#include "Engine/Texture2D.h"
#include "Shooter/ShooterCharacter.h"
#include "UI/UpgradeDescWidget.h"
#include "UI/UpgradeNodeWidget.h"
#include "Upgrade/OutlierUpgradeComponent.h"
#include "Upgrade/OutlierUpgradeSetData.h"

void UUpgradeNodeGroupWidget::NativeConstruct()
{
	Super::NativeConstruct();

	BindUpgradeStateChanged();
	BindPlayerStateNodeCountChanged();
	RefreshNodeWidgets();
}

void UUpgradeNodeGroupWidget::NativeDestruct()
{
	UnbindUpgradeStateChanged();
	UnbindPlayerStateNodeCountChanged();
	Super::NativeDestruct();
}

void UUpgradeNodeGroupWidget::InjectUpgradeContext(
	AShooterCharacter* InShooterCharacter,
	APartnerCharacter* InPartnerCharacter,
	UOutlierUpgradeComponent* InUpgradeComponent,
	AOutlierPlayerState* InPlayerState)
{
	if (UpgradeComponent != InUpgradeComponent)
	{
		UnbindUpgradeStateChanged();
	}

	if (PlayerState != InPlayerState)
	{
		UnbindPlayerStateNodeCountChanged();
	}

	ShooterCharacter = InShooterCharacter;
	PartnerCharacter = InPartnerCharacter;
	UpgradeComponent = InUpgradeComponent;
	PlayerState = InPlayerState;

	if (UpgradeComponent)
	{
		if (UpgradeSetData)
		{
			UpgradeComponent->SetUpgradeSetData(UpgradeSetData);
		}
		else
		{
			UpgradeComponent->SetUpgradeRole(GetResolvedUpgradeRole());
			if (UDataTable* ResolvedDataTable = GetResolvedUpgradeDataTable())
			{
				UpgradeComponent->SetUpgradeDataTable(ResolvedDataTable);
			}
		}
	}

	BindUpgradeStateChanged();
	BindPlayerStateNodeCountChanged();
	RefreshNodeWidgets();
}

void UUpgradeNodeGroupWidget::RefreshNodeWidgets()
{
	CacheNodeWidgets();

	for (UUpgradeNodeWidget* NodeWidget : NodeWidgets)
	{
		if (!NodeWidget)
		{
			continue;
		}

		if (UpgradeComponent)
		{
			NodeWidget->SetUpgradePlayerState(PlayerState);
			NodeWidget->SetUpgradeComponent(UpgradeComponent);
		}

		if (UpgradeDescWidgetClass)
		{
			NodeWidget->SetUpgradeDescWidgetClass(UpgradeDescWidgetClass);
		}

		NodeWidget->SetNodeTexture(ResolveNodeTexture(NodeWidget));
		NodeWidget->SetUnlockedNodeTexture(UnlockedNodeTexture);
		NodeWidget->SetDeactivatedNodeTexture(DeactivatedNodeTexture);
	}
}

EOutlierUpgradeRole UUpgradeNodeGroupWidget::GetResolvedUpgradeRole() const
{
	return UpgradeSetData
		? UpgradeSetData->UpgradeRole
		: UpgradeRole;
}

UDataTable* UUpgradeNodeGroupWidget::GetResolvedUpgradeDataTable() const
{
	return UpgradeSetData
		? UpgradeSetData->UpgradeDataTable.Get()
		: UpgradeDataTable.Get();
}

bool UUpgradeNodeGroupWidget::IsNodeIdSelectedByOtherWidget(
	const UUpgradeNodeWidget* RequestingWidget,
	FName InTreeId,
	FName InNodeId) const
{
	if (!NodeWidgetHost || !RequestingWidget || InTreeId.IsNone() || InNodeId.IsNone())
	{
		return false;
	}

	return IsNodeIdSelectedByOtherWidgetRecursive(NodeWidgetHost, RequestingWidget, InTreeId, InNodeId);
}

void UUpgradeNodeGroupWidget::HandleUpgradeStateChanged()
{
	RefreshNodeWidgets();
}

void UUpgradeNodeGroupWidget::BindUpgradeStateChanged()
{
	if (UpgradeComponent)
	{
		UpgradeComponent->OnUpgradeStateChanged.AddUniqueDynamic(this, &UUpgradeNodeGroupWidget::HandleUpgradeStateChanged);
	}
}

void UUpgradeNodeGroupWidget::UnbindUpgradeStateChanged()
{
	if (UpgradeComponent)
	{
		UpgradeComponent->OnUpgradeStateChanged.RemoveDynamic(this, &UUpgradeNodeGroupWidget::HandleUpgradeStateChanged);
	}
}

void UUpgradeNodeGroupWidget::BindPlayerStateNodeCountChanged()
{
	if (PlayerState && !NodeCountChangedHandle.IsValid())
	{
		NodeCountChangedHandle = PlayerState->OnNodeCountChanged.AddUObject(
			this,
			&UUpgradeNodeGroupWidget::HandleNodeCountChanged);
	}
}

void UUpgradeNodeGroupWidget::UnbindPlayerStateNodeCountChanged()
{
	if (PlayerState && NodeCountChangedHandle.IsValid())
	{
		PlayerState->OnNodeCountChanged.Remove(NodeCountChangedHandle);
	}

	NodeCountChangedHandle.Reset();
}

void UUpgradeNodeGroupWidget::HandleNodeCountChanged(int32 NewNodeCount)
{
	(void)NewNodeCount;
	RefreshNodeWidgets();
}

UTexture2D* UUpgradeNodeGroupWidget::ResolveNodeTexture(const UUpgradeNodeWidget* NodeWidget) const
{
	if (!NodeWidget)
	{
		return nullptr;
	}

	const FName RowName = NodeWidget->GetNodeRowName().IsNone()
		? NodeWidget->NodeRowName
		: NodeWidget->GetNodeRowName();
	return FindNodeTexture(RowName);
}

UTexture2D* UUpgradeNodeGroupWidget::FindNodeTexture(FName NodeRowName) const
{
	if (NodeRowName.IsNone())
	{
		return nullptr;
	}

	if (UpgradeSetData)
	{
		for (const FUpgradeNodeTextureBinding& Binding : UpgradeSetData->UnlockedNodeTextures)
		{
			if (Binding.NodeRowName == NodeRowName && Binding.Texture)
			{
				return Binding.Texture.Get();
			}
		}
	}

	return nullptr;
}

void UUpgradeNodeGroupWidget::CacheNodeWidgets()
{
	NodeWidgets.Reset();

	if (!NodeWidgetHost)
	{
		return;
	}

	CacheNodeWidgetsRecursive(NodeWidgetHost);
}

void UUpgradeNodeGroupWidget::CacheNodeWidgetsRecursive(UWidget* Widget)
{
	if (!Widget)
	{
		return;
	}

	if (UUpgradeNodeWidget* NodeWidget = Cast<UUpgradeNodeWidget>(Widget))
	{
		NodeWidgets.Add(NodeWidget);
	}

	UPanelWidget* PanelWidget = Cast<UPanelWidget>(Widget);
	if (!PanelWidget)
	{
		return;
	}

	const int32 ChildCount = PanelWidget->GetChildrenCount();
	for (int32 ChildIndex = 0; ChildIndex < ChildCount; ++ChildIndex)
	{
		CacheNodeWidgetsRecursive(PanelWidget->GetChildAt(ChildIndex));
	}
}

bool UUpgradeNodeGroupWidget::IsNodeIdSelectedByOtherWidgetRecursive(
	const UWidget* Widget,
	const UUpgradeNodeWidget* RequestingWidget,
	FName InTreeId,
	FName InNodeId) const
{
	if (!Widget)
	{
		return false;
	}

	if (const UUpgradeNodeWidget* NodeWidget = Cast<UUpgradeNodeWidget>(Widget))
	{
		if (NodeWidget != RequestingWidget
			&& DoesWidgetReferenceNode(NodeWidget, InTreeId, InNodeId))
		{
			return true;
		}
	}

	const UPanelWidget* PanelWidget = Cast<UPanelWidget>(Widget);
	if (!PanelWidget)
	{
		return false;
	}

	const int32 ChildCount = PanelWidget->GetChildrenCount();
	for (int32 ChildIndex = 0; ChildIndex < ChildCount; ++ChildIndex)
	{
		if (IsNodeIdSelectedByOtherWidgetRecursive(
			PanelWidget->GetChildAt(ChildIndex),
			RequestingWidget,
			InTreeId,
			InNodeId))
		{
			return true;
		}
	}

	return false;
}

bool UUpgradeNodeGroupWidget::DoesWidgetReferenceNode(
	const UUpgradeNodeWidget* NodeWidget,
	FName InTreeId,
	FName InNodeId) const
{
	if (!NodeWidget)
	{
		return false;
	}

	if (NodeWidget->TreeId == InTreeId && NodeWidget->NodeId == InNodeId)
	{
		return true;
	}

	UDataTable* ResolvedDataTable = GetResolvedUpgradeDataTable();
	if (!ResolvedDataTable || NodeWidget->NodeRowName.IsNone())
	{
		return false;
	}

	const FOutlierUpgradeNodeRow* Row = ResolvedDataTable->FindRow<FOutlierUpgradeNodeRow>(
		NodeWidget->NodeRowName,
		TEXT("UpgradeNodeGroupWidgetDuplicateCheck"),
		false);
	return Row && Row->TreeId == InTreeId && Row->NodeId == InNodeId;
}
