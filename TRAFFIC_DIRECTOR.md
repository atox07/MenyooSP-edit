# Traffic Director integrato

Apri Menyoo e vai in **Vehicle Options > Traffic Director**.

## Percorsi e collector

1. Vai in Manage Paths > Create Path (collector here). Il collector viene creato nella posizione del personaggio.
2. Regola **Collector Range (m)** con sinistra/destra: da 1 a 200 metri, in passi di 1 metro. Il cerchio giallo mostra il raggio effettivo. **Move Collector Here** ne sposta il centro.
3. Spostati e usa Add Waypoint Here per aggiungere almeno un punto di destinazione.
4. Con Automatic Collection attivo, i guidatori ambientali che entrano nel collector seguono il percorso. I veicoli del giocatore e delle missioni vengono esclusi.
5. Ogni waypoint ha velocita, raggio di arrivo, zona di cambio velocita e sosta regolabili.

La voce Collector / Waypoint 0 > Collection Radius modifica lo stesso raggio del collector.

## Spawner e zone di blocco

Manage Spawners consente di scegliere modello del veicolo, percorso, intervallo, limite di veicoli e orientamento.
Traffic Block Zones consente di creare zone di blocco, regolarne lunghezza/larghezza/altezza e spostarle o ruotarle. Le zone sono volumi rettangolari: si regolano tramite le dimensioni, anziche un raggio circolare.

## Salvataggi e uso

**Save Scene** salva percorsi, raggi dei collector, spawner, zone e impostazioni in `TrafficDirector/TrafficDirector.dat`, nella cartella di GTA V. Il formato rimane compatibile con le scene della versione standalone.
La scena salvata viene caricata alla prima apertura di Traffic Director. Dopo aver aperto l'editor, il traffico continua a essere gestito anche chiudendo il menu. Le anteprime sono visibili nelle schermate del Traffic Director se Show World Preview e attivo.
Usa i normali comandi di Menyoo per selezionare, tornare indietro e chiudere il menu. Rinomina e inserimento modello usano la tastiera a schermo del gioco.

Installa il nuovo Menyoo.asi con i normali file menyooStuff e Script Hook V. Rimuovi dalla cartella di GTA V il precedente TrafficDirector.asi se presente: le due versioni gestirebbero contemporaneamente il traffico.

## Compilazione

Rigenera il progetto da questa cartella con `Solution\external\premake\premake5.exe vs2022`, poi compila `Solution\Menyoo.sln` in Release x64. Il progetto Premake include automaticamente i nuovi file. Non usare Menyoobackup.sln.
Servono gli stessi strumenti e submodule del fork originale: Visual Studio C++, Windows SDK, pugixml, MinHook, simpleini e dirent. Il workflow gia presente in `.github/workflows/master_build.yml` esegue la generazione e la compilazione quando questa cartella e usata come repository Menyoo.

## Verifica in gioco

- Apri il nuovo sottomenu, crea un percorso con almeno due punti e modifica Collector Range.
- Verifica che il cerchio giallo cambi dimensione e che il traffico venga raccolto entro il nuovo raggio.
- Salva, cambia il raggio e ricarica: deve tornare al valore salvato.
- Prova ritorno, eliminazione di punti/percorsi e chiusura del menu con il traffico in movimento.

Verifica eseguita in questo ambiente: generazione Premake e inclusione dei nuovi sorgenti nel progetto. Compilazione C++ e prova in GTA V da eseguire su un ambiente con i requisiti sopra.
