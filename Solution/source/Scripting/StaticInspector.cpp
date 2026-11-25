#include "StaticInspector.h"

// Dipendenze volutamente minime: solo cose gia' usate in Routine.cpp / GTAmemory.h.
#include "..\Natives\types.h"
#include "..\Natives\natives2.h"
#include "..\Util\keyboard.h"
#include "..\Util\FileLogger.h"
#include "..\Util\ExePath.h"
#include "..\Scripting\enums.h"
#include "..\Scripting\Game.h"
#include "..\Scripting\GameplayCamera.h"
#include "..\Memory\GTAmemory.h"
#include "StaticModelSpawner.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <unordered_map>
#include <vector>

namespace StaticInspector
{
	namespace
	{
		constexpr float kMaxDistance = 200.0f;      // lunghezza del raycast
		constexpr float kPropFallbackRadius = 2.0f; // raggio ricerca prop vicino al punto colpito
		// 1 = mappa, 2 = veicoli, 4/8 = ped, 16 = oggetti (vegetazione esclusa)
		constexpr int kIntersectFlags = 1 | 2 | 4 | 8 | 16;

		// Hash delle native (stabili tra le build). Le chiamo direttamente tramite
		// nativeInit/nativePush64/nativeCall di ScriptHookV (gia' inclusi da GTAmemory.h),
		// cosi' il codice non dipende dai nomi usati nel tuo natives2.h.
		constexpr unsigned long long N_SHAPE_TEST_LOS = 0x377906D8A31E5586ULL; // START_EXPENSIVE_SYNCHRONOUS_SHAPE_TEST_LOS_PROBE
		constexpr unsigned long long N_SHAPE_TEST_RESULT = 0x3D87450E15D98694ULL; // GET_SHAPE_TEST_RESULT
		constexpr unsigned long long N_TEXT_FONT = 0x66E0276CC5F6B9DAULL;        // SET_TEXT_FONT
		constexpr unsigned long long N_TEXT_SCALE = 0x07C837F9A01C34C9ULL;       // SET_TEXT_SCALE
		constexpr unsigned long long N_TEXT_COLOUR = 0xBE6B23FFA53FB442ULL;      // SET_TEXT_COLOUR
		constexpr unsigned long long N_TEXT_OUTLINE = 0x2513DFB0FB8400FEULL;     // SET_TEXT_OUTLINE
		constexpr unsigned long long N_TEXT_BEGIN = 0x25FBB336DF1804CBULL;       // BEGIN_TEXT_COMMAND_DISPLAY_TEXT
		constexpr unsigned long long N_TEXT_ADD = 0x6C188BE134E074AAULL;         // ADD_TEXT_COMPONENT_SUBSTRING_PLAYER_NAME
		constexpr unsigned long long N_TEXT_END = 0xCD015E5BB0D96A57ULL;         // END_TEXT_COMMAND_DISPLAY_TEXT

		template<typename T>
		void PushArg(T value)
		{
			static_assert(sizeof(T) <= sizeof(unsigned long long), "argomento native troppo grande");
			unsigned long long raw = 0;
			std::memcpy(&raw, &value, sizeof(T));
			nativePush64(raw);
		}

		template<typename... Args>
		unsigned long long* CallNative(unsigned long long hash, Args... args)
		{
			nativeInit(hash);
			(PushArg(args), ...);
			return nativeCall();
		}

		// Layout richiesto dalle native per i vettori in uscita (3 float con padding).
		struct NativeVec3
		{
			float x; unsigned int px;
			float y; unsigned int py;
			float z; unsigned int pz;
		};

		struct P3
		{
			float x = 0, y = 0, z = 0;
		};

		// joaat calcolato qui (minuscolo), senza chiamare la funzione hash interna del gioco.
		unsigned int Joaat(const std::string& str)
		{
			unsigned int h = 0;
			for (unsigned char c : str)
			{
				h += static_cast<unsigned char>(std::tolower(c));
				h += (h << 10);
				h ^= (h >> 6);
			}
			h += (h << 3);
			h ^= (h >> 11);
			h += (h << 15);
			return h;
		}

		// GetModelInfo e' una funzione interna del gioco: la chiamo dentro __try/__except
		// cosi' un eventuale errore non fa crashare tutto; dopo il primo errore la disattivo.
		bool g_modelInfoFaulted = false;
		const CBaseModelInfo* SafeGetModelInfo(Hash h)
		{
			if (!GetModelInfo || g_modelInfoFaulted)
				return nullptr;
			__try
			{
				int idx = -1;
				return GetModelInfo(static_cast<unsigned int>(h), &idx);
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				g_modelInfoFaulted = true;
				return nullptr;
			}
		}

		// Evita la macro max di Windows.h
		inline float MaxF(float a, float b) { return a > b ? a : b; }

		float Dist3(const P3& a, const P3& b)
		{
			const float dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
			return std::sqrt(dx * dx + dy * dy + dz * dz);
		}

		bool g_active = false;
		bool g_namesLoaded = false;
		std::unordered_map<Hash, std::string> g_names;
		Hash g_lastLoggedHash = 0;

		// Candidato statico ricavato dalla tabella degli origin YMAP (un solo punto per modello).
		struct Candidate
		{
			Hash hash;
			float score; // piu' basso = piu' plausibile
			float dist;  // distanza orizzontale dall'origin
		};
		std::vector<Candidate> g_candidates;
		P3 g_candidatesFor;
		bool g_candidatesValid = false;

		struct HitInfo
		{
			bool hit = false;
			Entity entity = 0; // handle script; 0 per molti statici (edifici)
			P3 pos;            // punto colpito
			Hash modelHash = 0;
			bool fromPropFallback = false;
		};

		std::string ToLower(std::string s)
		{
			std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return s;
		}

		std::string HexHash(Hash h)
		{
			char buf[16];
			std::snprintf(buf, sizeof(buf), "0x%08X", static_cast<unsigned int>(h));
			return buf;
		}

		std::string NameFromHash(Hash h)
		{
			const auto it = g_names.find(h);
			return it != g_names.end() ? it->second : std::string();
		}

		// Raycast dal centro della camera.
		HitInfo CastFromCamera()
		{
			HitInfo out;

			const Vector3 camPos = GameplayCamera::GetPosition();
			const Vector3 camDir = GameplayCamera::GetDirectionFromScreenCentre();
			const float ex = camPos.x + camDir.x * kMaxDistance;
			const float ey = camPos.y + camDir.y * kMaxDistance;
			const float ez = camPos.z + camDir.z * kMaxDistance;

			int ignore = PLAYER_PED_ID();
			if (IS_PED_IN_ANY_VEHICLE(ignore, 0))
				ignore = GET_VEHICLE_PED_IS_IN(ignore, 0);

			const int ray = *reinterpret_cast<int*>(CallNative(N_SHAPE_TEST_LOS,
				static_cast<float>(camPos.x), static_cast<float>(camPos.y), static_cast<float>(camPos.z),
				ex, ey, ez, static_cast<int>(kIntersectFlags), ignore, static_cast<int>(7)));

			int hit = 0;
			NativeVec3 endCoords{}, normal{};
			int ent = 0;
			CallNative(N_SHAPE_TEST_RESULT, ray, &hit, &endCoords, &normal, &ent);

			out.hit = hit != 0;
			out.pos.x = endCoords.x;
			out.pos.y = endCoords.y;
			out.pos.z = endCoords.z;
			if (!out.hit)
				return out;

			if (ent != 0 && DOES_ENTITY_EXIST(ent))
			{
				out.entity = ent;
				out.modelHash = static_cast<Hash>(GET_ENTITY_MODEL(ent));
			}
			return out;
		}

		// Fallback: i prop "veri" (CObject) vicini al punto colpito. Non copre i CBuilding.
		void TryPropFallback(HitInfo& info)
		{
			std::vector<Entity> props;
			GTAmemory::GetPropHandles(props, Vector3(info.pos.x, info.pos.y, info.pos.z), kPropFallbackRadius);

			float bestDist = kPropFallbackRadius;
			Entity best = 0;
			for (Entity e : props)
			{
				if (!DOES_ENTITY_EXIST(e))
					continue;
				const Vector3 c = GET_ENTITY_COORDS(e, 1);
				P3 cp;
				cp.x = c.x; cp.y = c.y; cp.z = c.z;
				const float d = Dist3(cp, info.pos);
				if (d < bestDist)
				{
					bestDist = d;
					best = e;
				}
			}
			if (best != 0)
			{
				info.entity = best;
				info.modelHash = static_cast<Hash>(GET_ENTITY_MODEL(best));
				info.fromPropFallback = true;
			}
		}

		// Cerca nella tabella degli origin i modelli il cui ingombro contiene il punto colpito.
		// Usa solo archetipi gia' caricati (GetModelInfo != null), quindi cio' che e' in scena.
		// Limite: la tabella ha UN solo origin per modello, quindi le istanze multiple sfuggono.
		void FindStaticCandidates(const P3& hitPos)
		{
			g_candidates.clear();
			g_candidatesFor = hitPos;
			g_candidatesValid = true;
			if (!GetModelInfo)
				return;

			size_t count = 0;
			const auto* table = StaticModelSpawner::GetIndexedPlacements(count);
			if (!table)
				return;

			constexpr float kBroad = 300.0f;
			for (size_t i = 0; i < count; ++i)
			{
				const auto& o = table[i];
				const float dx = hitPos.x - o.x;
				const float dy = hitPos.y - o.y;
				if (std::fabs(dx) > kBroad || std::fabs(dy) > kBroad)
					continue;

				const CBaseModelInfo* info = SafeGetModelInfo(static_cast<Hash>(o.hash));
				if (!info)
					continue;

				const float ax = MaxF(std::fabs(info->m_aabbMin[0]), std::fabs(info->m_aabbMax[0]));
				const float ay = MaxF(std::fabs(info->m_aabbMin[1]), std::fabs(info->m_aabbMax[1]));
				const float hb = std::sqrt(ax * ax + ay * ay); // raggio orizzontale, valido per qualsiasi rotazione su Z
				const float dist = std::sqrt(dx * dx + dy * dy);
				if (dist > hb + 1.0f)
					continue;

				const float zRel = hitPos.z - o.z;
				if (zRel < info->m_aabbMin[2] - 1.0f || zRel > info->m_aabbMax[2] + 1.0f)
					continue;

				g_candidates.push_back({ static_cast<Hash>(o.hash), hb > 0.01f ? dist / hb : dist, dist });
			}

			if (g_modelInfoFaulted)
				addlog(ige::LogType::LOG_ERROR, "[Inspector] GetModelInfo ha generato un errore: ricerca candidati disattivata.");

			std::sort(g_candidates.begin(), g_candidates.end(),
				[](const Candidate& a, const Candidate& b) { return a.score < b.score; });
			if (g_candidates.size() > 3)
				g_candidates.resize(3);
		}

		void DrawCross(const P3& p)
		{
			const float s = 0.25f;
			DRAW_LINE(p.x - s, p.y, p.z, p.x + s, p.y, p.z, 255, 60, 60, 255);
			DRAW_LINE(p.x, p.y - s, p.z, p.x, p.y + s, p.z, 60, 255, 60, 255);
			DRAW_LINE(p.x, p.y, p.z - s, p.x, p.y, p.z + s, 60, 120, 255, 255);
		}

		void DrawText2D(const std::string& text, float x, float y)
		{
			CallNative(N_TEXT_FONT, static_cast<int>(4));
			CallNative(N_TEXT_SCALE, 0.0f, 0.38f);
			CallNative(N_TEXT_COLOUR, static_cast<int>(255), static_cast<int>(255), static_cast<int>(255), static_cast<int>(255));
			CallNative(N_TEXT_OUTLINE);
			CallNative(N_TEXT_BEGIN, "STRING");
			CallNative(N_TEXT_ADD, text.c_str());
			CallNative(N_TEXT_END, x, y, static_cast<int>(0));
		}

		void DrawLines(const std::vector<std::string>& lines)
		{
			float y = 0.40f;
			for (const auto& line : lines)
			{
				DrawText2D(line, 0.015f, y);
				y += 0.030f;
			}
		}
	}

	size_t LoadNames(const std::string& path)
	{
		g_namesLoaded = true;
		std::ifstream file(path);
		if (!file.is_open())
		{
			addlog(ige::LogType::LOG_WARNING, "[Inspector] Lista nomi non trovata: " + path + " (mostro solo gli hash).");
			return 0;
		}

		std::string line;
		while (std::getline(file, line))
		{
			while (!line.empty() && (line.back() == '\r' || line.back() == '\n' || line.back() == ' ' || line.back() == '\t'))
				line.pop_back();
			if (line.empty())
				continue;
			const std::string lower = ToLower(line);
			g_names.emplace(static_cast<Hash>(Joaat(lower)), line);
		}
		addlog(ige::LogType::LOG_INFO, "[Inspector] Caricati " + std::to_string(g_names.size()) + " nomi modello da " + path);
		return g_names.size();
	}

	void Toggle()
	{
		g_active = !g_active;
		addlog(ige::LogType::LOG_INFO, std::string("[Inspector] Toggle -> ") + (g_active ? "ON" : "OFF"));
		g_lastLoggedHash = 0;
		g_candidatesValid = false;
		if (g_active && !g_namesLoaded)
			LoadNames(GetPathffA(Pathff::Main, true) + "allarchetypes.txt");
		Game::Print::PrintBottomLeft(std::string(g_active ? "Inspector: ~g~ON" : "Inspector: ~r~OFF"));
	}

	bool IsActive()
	{
		return g_active;
	}

	void Tick()
	{
		if (IsKeyJustUp(VirtualKey::N))
			Toggle();

		if (!g_active)
			return;

		HitInfo info = CastFromCamera();
		if (info.hit && info.modelHash == 0)
			TryPropFallback(info);

		std::vector<std::string> lines;
		lines.push_back("~y~INSPECTOR~s~ (N per uscire)");

		if (!info.hit)
		{
			lines.push_back("Nessun colpo (nulla entro " + std::to_string(static_cast<int>(kMaxDistance)) + " m)");
			DrawLines(lines);
			return;
		}

		DrawCross(info.pos);

		if (info.modelHash != 0)
		{
			const std::string name = NameFromHash(info.modelHash);
			lines.push_back("Modello: ~g~" + (name.empty() ? std::string("(nome sconosciuto)") : name));
			lines.push_back("Hash: " + HexHash(info.modelHash) + "  (" + std::to_string(static_cast<unsigned long>(info.modelHash)) + ")");
			if (info.fromPropFallback)
				lines.push_back("~o~Prop vicino al punto colpito (approssimato)");
			if (info.entity != 0)
				lines.push_back("Handle: " + std::to_string(info.entity));

			if (info.modelHash != g_lastLoggedHash)
			{
				g_lastLoggedHash = info.modelHash;
				addlog(ige::LogType::LOG_INFO, "[Inspector] " + (name.empty() ? std::string("?") : name) + " " + HexHash(info.modelHash));
			}
		}
		else
		{
			// Statico senza handle: ricalcolo i candidati solo se il punto colpito si e' spostato di oltre 1 m.
			if (!g_candidatesValid || Dist3(info.pos, g_candidatesFor) > 1.0f)
				FindStaticCandidates(info.pos);

			if (g_candidates.empty())
			{
				lines.push_back("~r~Statico senza handle e nessun candidato nella tabella origin");
			}
			else
			{
				lines.push_back("~o~Statico: candidati dagli origin YMAP (euristica)");
				for (const auto& c : g_candidates)
				{
					const std::string name = NameFromHash(c.hash);
					char dist[32];
					std::snprintf(dist, sizeof(dist), "  [%.1f m]", c.dist);
					lines.push_back("~g~" + (name.empty() ? HexHash(c.hash) : name) + "~s~ " + HexHash(c.hash) + dist);
				}
				if (g_candidates.front().hash != g_lastLoggedHash)
				{
					g_lastLoggedHash = g_candidates.front().hash;
					const std::string name = NameFromHash(g_lastLoggedHash);
					addlog(ige::LogType::LOG_INFO, "[Inspector] candidato statico " + (name.empty() ? std::string("?") : name) + " " + HexHash(g_lastLoggedHash));
				}
			}
		}

		char pos[96];
		std::snprintf(pos, sizeof(pos), "Pos: %.2f, %.2f, %.2f", info.pos.x, info.pos.y, info.pos.z);
		lines.push_back(pos);

		DrawLines(lines);
	}
}
