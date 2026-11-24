#include "StaticArchetypeStreaming.h"

#include "..\Memory\GTAmemory.h"
#include "..\Natives\natives2.h"
#include "..\Util\FileLogger.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <unordered_map>

namespace StaticArchetypeStreaming
{
	namespace
	{
		// Verified against the running Legacy 1.0.3889.0 executable.
		// See FiveM's Streaming.cpp and ArchetypesCollector.cpp for the streamer API.
		struct StreamEntry
		{
			uint32_t handle;
			uint32_t flags;
		};

		using ModuleLookup = void* (*)(void*, const char*);
		using Request = void (*)(void*, uint32_t, int);
		using LoadObjectsNow = void (*)(bool);
		using ReleaseRequest = bool (*)(void*, uint32_t);
		using ChangeRef = void (*)(void*, uint32_t);

		constexpr uint32_t batchSize = 4;
		struct TypesStore
		{
			void* manager = nullptr;
			void* module = nullptr;
			Request request = nullptr;
			LoadObjectsNow loadObjectsNow = nullptr;
			ReleaseRequest releaseRequest = nullptr;
			ChangeRef addRef = nullptr;
			ChangeRef removeRef = nullptr;
			StreamEntry* entries = nullptr;
			const uint8_t* slotFlags = nullptr;
			uint32_t baseIndex = 0;
			uint32_t slotCount = 0;
		};

		using Slots = std::array<uint32_t, batchSize>;
		struct RetainedBatch
		{
			Slots slots{};
			uint32_t count = 0;
		};

		std::unordered_map<Hash, RetainedBatch> retainedBatches;
		std::unordered_map<Hash, uint32_t> nextSearchSlot;

		template<typename T>
		T ReadAt(const void* base, size_t offset)
		{
			T result;
			std::memcpy(&result, static_cast<const uint8_t*>(base) + offset, sizeof(result));
			return result;
		}

		bool GetTypesStore(TypesStore& out)
		{
			if (g_isEnhanced || GTAmemory::GetGameVersion() != VER_1_0_3889_0)
				return false;

			const auto* image = reinterpret_cast<const uint8_t*>(GetModuleHandleW(nullptr));
			if (!image || g_MainModuleInfo.SizeOfImage <= 0x2EA2700)
				return false;

			constexpr unsigned char lookupBytes[] = { 0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xC2 };
			constexpr unsigned char requestBytes[] = { 0x89, 0x54, 0x24, 0x10, 0x55, 0x53, 0x56, 0x57 };
			constexpr unsigned char releaseBytes[] = { 0x48, 0x89, 0x5C, 0x24, 0x18, 0x89, 0x54, 0x24, 0x10 };
			constexpr unsigned char moduleManagerLea[] = { 0x48, 0x8D, 0x0D, 0xCF, 0xBE, 0x80, 0x01 };
			// b3889: RequestObject, xor ecx, ecx, call LoadObjectsNow.
			constexpr unsigned char requestAndLoad[] = { 0xE8, 0x37, 0xE1, 0x24, 0x01, 0x33, 0xC9, 0xE8, 0x38, 0x27, 0x3B, 0x00 };
			if (std::memcmp(image + 0x169410C, lookupBytes, sizeof(lookupBytes)) ||
				std::memcmp(image + 0x452268, requestAndLoad, sizeof(requestAndLoad)) ||
				std::memcmp(image + 0x16A03A4, requestBytes, sizeof(requestBytes)) ||
				std::memcmp(image + 0x169F210, releaseBytes, sizeof(releaseBytes)) ||
				std::memcmp(image + 0x16966B2, moduleManagerLea, sizeof(moduleManagerLea)))
				return false;

			out.manager = const_cast<uint8_t*>(image + 0x2EA23D0);
			auto getModule = reinterpret_cast<ModuleLookup>(image + 0x169410C);
			out.module = getModule(static_cast<uint8_t*>(out.manager) + 0x1B8, "ytyp");
			if (!out.module)
				return false;

			const auto* methods = ReadAt<const uintptr_t*>(out.module, 0);
			if (!methods || methods[0xB0 / 8] != reinterpret_cast<uintptr_t>(image + 0x168BC70) ||
				methods[0xB8 / 8] != reinterpret_cast<uintptr_t>(image + 0x16BF3B4))
				return false;

			out.request = reinterpret_cast<Request>(image + 0x16A03A4);
			out.loadObjectsNow = reinterpret_cast<LoadObjectsNow>(image + 0x8049AC);
			out.releaseRequest = reinterpret_cast<ReleaseRequest>(image + 0x169F210);
			out.addRef = reinterpret_cast<ChangeRef>(methods[0xB0 / 8]);
			out.removeRef = reinterpret_cast<ChangeRef>(methods[0xB8 / 8]);
			out.entries = ReadAt<StreamEntry*>(out.manager, 0);
			const auto entryCount = ReadAt<uint32_t>(out.manager, 0x18);
			out.baseIndex = ReadAt<uint32_t>(out.module, 0x08);
			out.slotFlags = ReadAt<const uint8_t*>(out.module, 0x40);
			out.slotCount = ReadAt<uint32_t>(out.module, 0x48);
			return out.entries && out.slotFlags && out.slotCount > 0 &&
				out.baseIndex < entryCount && out.slotCount <= entryCount - out.baseIndex;
		}

		bool IsAvailable(Hash hash)
		{
			int index = -1;
			return (GetModelInfo && GetModelInfo(hash, &index)) || IS_MODEL_IN_CDIMAGE(hash);
		}

		uint32_t State(const TypesStore& store, uint32_t slot)
		{
			return store.entries[store.baseIndex + slot].flags & 3;
		}
	}

	bool Resolve(Hash hash, unsigned long timeoutMs)
	{
		if (NETWORK_IS_SESSION_ACTIVE())
			return false;
		if (retainedBatches.contains(hash))
			return IsAvailable(hash);

		TypesStore store;
		if (!GetTypesStore(store))
		{
			addlog(ige::LogType::LOG_WARNING, "[YTYP] Streaming layout unavailable for this executable.");
			return false;
		}

		const ULONGLONG deadline = GetTickCount64() + timeoutMs;
		uint32_t cursor = nextSearchSlot[hash];
		if (cursor >= store.slotCount)
			cursor = 0;
		uint32_t requested = 0;
		uint32_t completed = 0;
		addlog(ige::LogType::LOG_INFO, "[YTYP] Searching for " + std::to_string(hash) + " from slot " + std::to_string(cursor));
		while (cursor < store.slotCount && GetTickCount64() < deadline)
		{
			RetainedBatch batch;
			while (cursor < store.slotCount && batch.count < batchSize)
			{
				const uint32_t slot = cursor++;
				const auto& entry = store.entries[store.baseIndex + slot];
				if ((store.slotFlags[slot] & 0x80) || entry.handle == 0 || entry.handle == UINT32_MAX || State(store, slot) != 0)
					continue;
				batch.slots[batch.count++] = slot;
				store.request(store.manager, store.baseIndex + slot, 0);
				++requested;
			}
			if (!batch.count)
				break;

			store.loadObjectsNow(false);

			const bool available = IsAvailable(hash);
			RetainedBatch loaded;
			for (uint32_t i = 0; i < batch.count; ++i)
			{
				const auto slot = batch.slots[i];
				const bool ready = State(store, slot) == 1;
				if (ready)
					++completed;
				if (available && ready)
				{
					store.addRef(store.module, slot);
					loaded.slots[loaded.count++] = slot;
					addlog(ige::LogType::LOG_INFO, "[YTYP] Retained streaming index " + std::to_string(store.baseIndex + slot));
				}
				else
					store.releaseRequest(store.manager, store.baseIndex + slot);
			}
			if (available && loaded.count && IsAvailable(hash))
			{
				retainedBatches.emplace(hash, loaded);
				nextSearchSlot.erase(hash);
				addlog(ige::LogType::LOG_INFO, "[YTYP] Archetype " + std::to_string(hash) + " available after " + std::to_string(requested) + " package requests.");
				return true;
			}
			for (uint32_t i = 0; i < loaded.count; ++i)
			{
				const auto slot = loaded.slots[i];
				store.releaseRequest(store.manager, store.baseIndex + slot);
				store.removeRef(store.module, slot);
			}
			scriptWait(0);
		}

		if (cursor < store.slotCount)
			nextSearchSlot[hash] = cursor;
		else
			nextSearchSlot.erase(hash);
		addlog(ige::LogType::LOG_WARNING, "[YTYP] Archetype " + std::to_string(hash) +
			": loaded " + std::to_string(completed) + "/" + std::to_string(requested) +
			" requested packages; slot " + std::to_string(cursor) + "/" + std::to_string(store.slotCount) +
			(cursor < store.slotCount ? " (time limit; retry resumes here)." : " (reached end)."));
		return false;
	}

	void Release(Hash hash)
	{
		const auto it = retainedBatches.find(hash);
		if (it == retainedBatches.end())
			return;
		TypesStore store;
		if (!GetTypesStore(store))
			return;
		for (uint32_t i = 0; i < it->second.count; ++i)
		{
			const auto slot = it->second.slots[i];
			if (slot < store.slotCount && !(store.slotFlags[slot] & 0x80))
			{
				store.releaseRequest(store.manager, store.baseIndex + slot);
				store.removeRef(store.module, slot);
			}
		}
		retainedBatches.erase(it);
	}

	void Clear()
	{
		nextSearchSlot.clear();
		while (!retainedBatches.empty())
		{
			const auto hash = retainedBatches.begin()->first;
			Release(hash);
			if (retainedBatches.contains(hash))
				break;
		}
	}
}
