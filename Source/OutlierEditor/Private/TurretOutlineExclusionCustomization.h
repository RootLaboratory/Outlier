#pragma once

#include "IPropertyTypeCustomization.h"

class SWidget;

class FTurretOutlineExclusionCustomization final : public IPropertyTypeCustomization
{
public:
	static TSharedRef<IPropertyTypeCustomization> MakeInstance();
	virtual void CustomizeHeader(TSharedRef<IPropertyHandle> PropertyHandle, FDetailWidgetRow& HeaderRow,
		IPropertyTypeCustomizationUtils& Utils) override;
	virtual void CustomizeChildren(TSharedRef<IPropertyHandle> PropertyHandle, IDetailChildrenBuilder& ChildBuilder,
		IPropertyTypeCustomizationUtils& Utils) override;

private:
	TSharedPtr<IPropertyHandle> MeshHandle;
	TSharedRef<SWidget> BuildMeshMenu();
	FText GetMeshText() const;
	void SelectMesh(FName ComponentName);
};
