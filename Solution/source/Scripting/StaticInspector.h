#pragma once

#include <string>

// Modalita' "ispeziona" stile CodeWalker: premi N, punti qualcosa e vedi nome + hash del modello.
namespace StaticInspector
{
	// Da chiamare una volta per frame (dal loop principale, dopo Menu::Tick()).
	void Tick();

	void Toggle();
	bool IsActive();

	// Carica un file con un nome modello per riga (es. allarchetypes.txt di CodeWalker).
	// Chiamata automaticamente alla prima attivazione. Ritorna il numero di nomi caricati.
	size_t LoadNames(const std::string& path);
}
