#pragma once

#include "CoreMinimal.h"

class SWindow;

// Owned by the editor module so Slate callbacks cannot outlive the module.
class FOutlierStableIdTool
{
public:
	void Open();
	void CheckBeforePIE(bool bIsSimulating);
	void Shutdown();

private:
	void ShowReport(bool bOnlyIfInvalid);
	TWeakPtr<SWindow> Window;
};
