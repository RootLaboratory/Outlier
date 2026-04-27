#pragma once

#include "CoreMinimal.h"

class SWindow;

class FRDGDebugWindowManager
{
public:
	void OpenWindow();
	void CloseWindow();

private:
	TWeakPtr<SWindow> DebugWindow;
};
