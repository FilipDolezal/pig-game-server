# Dokumentace - Hra Pig (Kostková hra)

**Autor:** Filip Doležal
**Předmět:** UPS - Úvod do počítačových sítí
**Semestr:** ZS 2024/2025

---

## Obsah

1. [Popis hry](#1-popis-hry)
2. [Protokol](#2-protokol)
   - [Formát zpráv](#21-formát-zpráv)
   - [Příkazy klient → server](#22-příkazy-klient--server)
   - [Odpovědi server → klient](#23-odpovědi-server--klient)
   - [Klíče (keys) pro parametry](#24-klíče-keys-pro-parametry)
   - [Chybové stavy](#25-chybové-stavy)
   - [Stavový diagram](#26-stavový-diagram)
   - [Omezení a validace](#27-omezení-a-validace)
3. [Implementace serveru](#3-implementace-serveru)
   - [Dekompozice do modulů](#31-dekompozice-do-modulů)
   - [Vrstvy aplikace](#32-vrstvy-aplikace)
   - [Paralelizace](#33-paralelizace)
   - [Použité knihovny](#34-použité-knihovny)
4. [Implementace klienta](#4-implementace-klienta)
   - [Dekompozice do tříd](#41-dekompozice-do-tříd)
   - [Vrstvy aplikace](#42-vrstvy-aplikace)
   - [Použité knihovny](#43-použité-knihovny)
5. [Překlad a spuštění](#5-překlad-a-spuštění)
   - [Požadavky](#51-požadavky)
   - [Překlad serveru](#52-překlad-serveru)
   - [Překlad klienta](#53-překlad-klienta)
   - [Spuštění](#54-spuštění)
6. [Závěr](#6-závěr)

---

## 1. Popis hry

### Pig (Kostková hra)

Pig je jednoduchá kostková hra pro **2 hráče**. Cílem hry je jako první dosáhnout **30 bodů** (konfigurovatelné).

### Pravidla

1. Hráči se střídají v tazích
2. Na tahu může hráč:
   - **ROLL (hodit)** - hodí kostkou (1-6)
     - Pokud padne **1**: hráč ztrácí všechny body získané v tomto tahu a tah končí
     - Pokud padne **2-6**: hodnota se přičte k bodům tahu, hráč může pokračovat
   - **HOLD (zastavit)** - ukončí tah a přičte body tahu ke svému celkovému skóre
3. Kdo první dosáhne 30 bodů, vyhrává

### Herní mechaniky

- **Reconnect:** Pokud hráč ztratí spojení během hry, má 20 sekund na opětovné připojení
- **Idle timeout:** Hráč nečinný déle než 20 sekund je odpojen
- **Heartbeat:** Klient musí posílat PING alespoň každých 10 sekund

---

## 2. Protokol

### 2.1 Formát zpráv

Zprávy jsou textové, zakončené znakem `\n` (LF). Formát:

```
PŘÍKAZ|klíč1:hodnota1|klíč2:hodnota2\n
```

- **PŘÍKAZ** - název příkazu (velkými písmeny)
- **|** - oddělovač parametrů
- **klíč:hodnota** - páry parametrů
- **\n** - konec zprávy

**Příklad:**
```
GAME_STATE|my_score:10|opp_score:5|turn_score:3|roll:4|your_turn:1\n
```

### 2.2 Příkazy klient → server

| Příkaz | Parametry | Popis |
|--------|-----------|-------|
| `LOGIN` | `nick:<přezdívka>` | Přihlášení hráče |
| `RESUME` | - | Obnovení pozastavené hry po reconnectu |
| `LIST_ROOMS` | - | Získat seznam místností |
| `JOIN_ROOM` | `room:<id>` | Připojit se do místnosti |
| `LEAVE_ROOM` | - | Opustit místnost (pouze v čekání) |
| `ROLL` | - | Hodit kostkou |
| `HOLD` | - | Ukončit tah |
| `QUIT` | - | Vzdát hru |
| `EXIT` | - | Odpojit se ze serveru |
| `PING` | - | Heartbeat |
| `GAME_STATE_REQUEST` | - | Vyžádat aktuální stav hry |

### 2.3 Odpovědi server → klient

| Odpověď | Parametry | Popis |
|---------|-----------|-------|
| `WELCOME` | `players:<max>`, `rooms:<max>` | Uvítání po připojení |
| `OK` | `cmd:<příkaz>`, [další] | Potvrzení úspěšného příkazu |
| `ERROR` | `msg:<chyba>`, `cmd:<příkaz>` | Chybová odpověď |
| `ROOM_INFO` | `room:<id>`, `count:<počet>`, `state:<stav>` | Info o místnosti |
| `GAME_START` | `opp_nick:<přezdívka>`, `your_turn:<0|1>` | Začátek hry |
| `GAME_STATE` | `my_score`, `opp_score`, `turn_score`, `roll`, `your_turn` | Stav hry |
| `GAME_WIN` | `msg:<zpráva>` (volitelné) | Výhra |
| `GAME_LOSE` | `msg:<zpráva>` (volitelné) | Prohra |
| `GAME_PAUSED` | - | Hra pozastavena (reconnect flow) |
| `OPPONENT_DISCONNECTED` | - | Soupeř se odpojil |
| `OPPONENT_RECONNECTED` | - | Soupeř se znovu připojil |
| `DISCONNECTED` | - | Odpojení ze serveru |

### 2.4 Klíče (keys) pro parametry

| Klíč | Datový typ | Popis |
|------|------------|-------|
| `cmd` | string | Název příkazu |
| `msg` | string | Textová zpráva/chyba |
| `nick` | string (max 31 znaků) | Přezdívka hráče |
| `room` | int (0 až MAX_ROOMS-1) | ID místnosti |
| `state` | enum | Stav místnosti: WAITING, IN_PROGRESS, PAUSED, ABORTED |
| `count` | int (0-2) | Počet hráčů v místnosti |
| `opp_nick` | string | Přezdívka soupeře |
| `your_turn` | int (0 nebo 1) | 1 = je váš tah |
| `my_score` | int (0-30+) | Vaše skóre |
| `opp_score` | int (0-30+) | Skóre soupeře |
| `turn_score` | int (0+) | Body v aktuálním tahu |
| `roll` | int (1-6) | Výsledek hodu kostkou |
| `players` | int | Max počet hráčů na serveru |
| `rooms` | int | Max počet místností |

### 2.5 Chybové stavy

| Chyba | Kdy nastane |
|-------|-------------|
| `INVALID_COMMAND` | Neplatný nebo nerozpoznaný příkaz |
| `INVALID_NICKNAME` | Prázdná nebo příliš dlouhá přezdívka |
| `SERVER_FULL` | Server dosáhl limitu hráčů |
| `ROOM_FULL` | Místnost je plná |
| `GAME_IN_PROGRESS` | Nelze opustit místnost - hra běží |
| `CANNOT_JOIN` | Nelze se připojit do místnosti |
| `NICKNAME_IN_USE` | Přezdívka je již používána |

### 2.6 Stavový diagram

```
                              ┌─────────────┐
                              │ DISCONNECTED│
                              └──────┬──────┘
                                     │ connect
                                     ▼
                              ┌─────────────┐
                              │  CONNECTED  │
                              └──────┬──────┘
                                     │ LOGIN
                          ┌──────────┴──────────┐
                          │                     │
                          ▼                     ▼
                   ┌─────────────┐       ┌─────────────┐
                   │    LOBBY    │       │ GAME_PAUSED │ (reconnect)
                   └──────┬──────┘       └──────┬──────┘
                          │                     │ RESUME
                          │ JOIN_ROOM           │
                          ▼                     │
                   ┌─────────────┐              │
                   │   WAITING   │◄─────────────┘
                   └──────┬──────┘
                          │ (2. hráč se připojí)
                          ▼
                   ┌─────────────┐
                   │   IN_GAME   │◄──┐
                   └──────┬──────┘   │
                          │          │ OPPONENT_RECONNECTED
                          │          │
              ┌───────────┼───────────┐
              │           │           │
              ▼           ▼           ▼
         GAME_WIN    GAME_LOSE   ┌─────────┐
              │           │      │ PAUSED  │
              └─────┬─────┘      └────┬────┘
                    │                 │ timeout
                    ▼                 ▼
             ┌─────────────┐    GAME_WIN/LOSE
             │    LOBBY    │
             └─────────────┘
```

### 2.7 Omezení a validace

| Parametr | Omezení |
|----------|---------|
| Délka zprávy | max 256 bytů |
| Přezdívka | max 31 znaků, neprázdná |
| ID místnosti | 0 až MAX_ROOMS-1 |
| Výsledek hodu | 1-6 |
| Skóre | 0+ (výhra při ≥30) |
| PING interval | max 10 sekund |
| Idle timeout | 20 sekund |
| Reconnect timeout | 20 sekund |

---

## 3. Implementace serveru

Server je implementován v jazyce **C** (standard C11).

### 3.1 Dekompozice do modulů

```
server/
├── include/
│   ├── config.h      # Konfigurační konstanty
│   ├── server.h      # Hlavní serverové funkce
│   ├── lobby.h       # Správa hráčů a místností
│   ├── game.h        # Herní logika (Pig)
│   ├── protocol.h    # Serializace zpráv, TCP buffering
│   ├── parser.h      # Parsování příkazů
│   └── logger.h      # Logování
└── src/
    ├── main.c        # Entry point, argument parsing
    ├── server.c      # Accept loop, klientská a herní vlákna
    ├── lobby.c       # Správa hráčů, místností, reconnect
    ├── game.c        # Pravidla hry Pig
    ├── protocol.c    # Odesílání/příjem zpráv
    ├── parser.c      # Tokenizace příkazů
    └── logger.c      # Thread-safe logování
```

### 3.2 Vrstvy aplikace

```
┌─────────────────────────────────────┐
│            main.c                   │  Vstupní bod, konfigurace
├─────────────────────────────────────┤
│           server.c                  │  Síťová vrstva, vlákna
├─────────────────────────────────────┤
│     lobby.c          game.c         │  Aplikační logika
├─────────────────────────────────────┤
│   protocol.c        parser.c        │  Protokol, serializace
├─────────────────────────────────────┤
│           logger.c                  │  Utility
└─────────────────────────────────────┘
```

### 3.3 Paralelizace

Server využívá **vlákna (pthreads)**:

1. **Hlavní vlákno** - accept loop, přijímá nová spojení
2. **Klientská vlákna** (client_handler_thread) - jedno vlákno na klienta
   - Zpracovává LOGIN, lobby příkazy
   - Čeká na začátek/konec hry
3. **Herní vlákna** (game_thread_func) - jedno vlákno na aktivní hru
   - Řídí herní smyčku
   - Zpracovává ROLL, HOLD
   - Řeší disconnect/reconnect, idle timeout

**Synchronizace:**
- `lobby_mutex` - chrání globální struktury (players, rooms)
- `room->mutex` + `room->cond` - synchronizace mezi klientským a herním vláknem

**I/O multiplexing:**
- `select()` s timeoutem pro neblokující čtení ze socketů

### 3.4 Použité knihovny

| Knihovna | Účel |
|----------|------|
| `pthread.h` | POSIX vlákna |
| `sys/socket.h` | BSD sockety |
| `arpa/inet.h` | Síťové funkce |
| `time.h` | Časové funkce |
| `stdarg.h` | Variabilní argumenty (send_structured_message) |

---

## 4. Implementace klienta

Klient je implementován v jazyce **Java** (verze 14+).

### 4.1 Dekompozice do tříd

```
client/src/
├── Main.java                    # Entry point
├── controller/
│   ├── ViewController.java      # Řízení UI
│   ├── NetworkController.java   # Síťová komunikace
│   ├── ViewToNetworkInterface.java
│   └── NetworkToViewInterface.java
├── model/
│   └── room/
│       └── GameRoomStatus.java  # Enum stavů místnosti
├── net/
│   ├── Client.java              # TCP klient, heartbeat
│   ├── Protocol.java            # Definice protokolu
│   ├── ServerMessage.java       # Parsování odpovědí
│   └── msg/
│       ├── ClientMessage.java   # Základní třída zpráv
│       ├── MsgLogin.java        # LOGIN zpráva
│       ├── MsgJoinRoom.java     # JOIN_ROOM zpráva
│       ├── MsgRoll.java         # ROLL zpráva
│       ├── MsgHold.java         # HOLD zpráva
│       ├── MsgPing.java         # PING zpráva
│       └── ...
└── view/
    ├── MainFrame.java           # Hlavní okno
    ├── LoginView.java           # Přihlašovací obrazovka
    ├── LobbyView.java           # Lobby (seznam místností)
    └── GameView.java            # Herní obrazovka
```

### 4.2 Vrstvy aplikace

```
┌─────────────────────────────────────┐
│              view/                  │  Prezentační vrstva (Swing)
├─────────────────────────────────────┤
│           controller/               │  Řídící vrstva
├─────────────────────────────────────┤
│              net/                   │  Síťová vrstva
├─────────────────────────────────────┤
│             model/                  │  Datový model
└─────────────────────────────────────┘
```

**Architektura:** MVC (Model-View-Controller)

- **View** - Swing GUI komponenty
- **Controller** - ViewController (UI logika), NetworkController (síť)
- **Model** - GameRoomStatus, data z protokolu

### 4.3 Použité knihovny

| Knihovna | Účel |
|----------|------|
| `javax.swing` | GUI framework |
| `java.net.Socket` | TCP komunikace |
| `java.io` | I/O streamy |
| `java.util` | Kolekce, utility |

---

## 5. Překlad a spuštění

### 5.1 Požadavky

**Server:**
- GCC (C11 kompatibilní)
- CMake 3.10+
- pthread knihovna (POSIX)
- Linux/Unix prostředí

**Klient:**
- Java JDK 14+
- CMake 3.10+ (nebo ruční kompilace)

### 5.2 Překlad serveru

```bash
cd server

# Vytvoření build adresáře a konfigurace
cmake -B build -S .

# Kompilace
cmake --build build

# Spustitelný soubor: build/server
```

### 5.3 Překlad klienta

**Pomocí CMake:**
```bash
cd client

# Konfigurace a kompilace
cmake -S . -B build
cmake --build build --target jar

# Výsledný JAR: build/sp-client.jar
```

**Pomocí Makefile:**
```bash
cd client
make run  # Zkompiluje a spustí
```

**Ruční kompilace:**
```bash
cd client/src
javac -d ../build/classes $(find . -name "*.java")
jar cfe ../build/sp-client.jar Main -C ../build/classes .
```

### 5.4 Spuštění

**Server:**
```bash
./server [OPTIONS] [PORT]

Volby:
  -a ADDRESS    IP adresa pro binding (default: 0.0.0.0)
  -p MAX_PLAYERS  Max počet hráčů (default: 10)
  -r MAX_ROOMS    Max počet místností (default: 5)
  -l LOGDIR       Adresář pro logy (default: logs/)

Příklad:
  ./server -p 20 -r 10 12345
```

**Klient:**
```bash
java -jar sp-client.jar
```

Klient zobrazí GUI pro zadání IP adresy, portu a přezdívky.

---

## 6. Závěr

### Dosažené výsledky

Aplikace úspěšně implementuje:

- **Funkční hru Pig** pro 2 hráče přes síť
- **Robustní protokol** s validací dat a ošetřením chyb
- **Reconnect mechanismus** - hráč se může vrátit do rozehrané hry do 20 sekund
- **Heartbeat systém** - detekce nečinných/odpojených hráčů
- **Thread-safe server** - zvládá více současných her
- **Grafický klient** s intuitivním rozhraním

### Architektonická rozhodnutí

1. **Textový protokol** - snadné ladění, čitelnost (oproti binárnímu)
2. **Vlákna místo select/poll** - jednodušší kód, každý klient má vlastní kontext
3. **Oddělené herní vlákno** - herní logika nezávislá na lobby operacích
4. **TCP buffering** - správné zpracování fragmentovaných zpráv

### Testování

Aplikace byla testována na:
- Ubuntu 22.04 (WSL2)
- Java 14, 17
- GCC 11.4.0
- Různé scénáře disconnect/reconnect
