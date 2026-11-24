#pragma once

#include "..\Natives\types.h"

namespace StaticArchetypeStreaming
{
	bool Resolve(Hash hash, unsigned long timeoutMs);
	void Release(Hash hash);
	void Clear();
}
