# Dystopia MUD — Zone Map

Connections between all zones. Arrows show traversable exits; `<-->` means bidirectional, `-->` means one-way.

```mermaid
graph TD
    %% Central Hub
    MID["🏰 MIDGAARD\n(3000-29503)"]

    %% === DIRECTLY CONNECTED TO MIDGAARD ===
    SMURF["SMURF\n(100-129)"]
    HEAVEN["HEAVEN\n(99000-99100)"]
    SCHOOL["SCHOOL\n(3700-3760)"]
    SEWER["SEWER\n(7000-7445)"]
    DREAM["DREAM\n(8600-8699)"]
    MOBFACT["MOB FACTORY\n(9401-9460)"]

    MID <--> SMURF
    MID <--> HEAVEN
    MID --> SCHOOL
    MID <--> SEWER
    DREAM --> MID
    MOBFACT <--> MID

    %% === HAON CLUSTER ===
    HAON["⚔️ HAON\n(6000-6155)"]
    SHIRE["SHIRE\n(1100-1157)"]
    HELL["HELL\n(30100-30200)"]
    TROLLDEN["TROLLDEN\n(2800-2808)"]
    MARSH["MARSH\n(8301-8382)"]
    ARACHNOS["ARACHNOS\n(6200-6399)"]
    HITOWER["🗼 HITOWER\n(1300-1425)"]

    MID <--> HAON
    HAON <--> SHIRE
    SHIRE --> MID
    HAON <--> HELL
    HAON <--> TROLLDEN
    HAON <--> MARSH
    HAON <--> ARACHNOS
    HAON <--> HITOWER
    HITOWER --> MID

    %% === HITOWER BRANCHES ===
    DROW["DROW\n(5100-5199)"]
    DYLAN["DYLAN\n(9101-9199)"]
    REDFERNE["REDFERNE\n(7900-7918)"]
    OLYMPUS["OLYMPUS\n(901-970)"]

    HITOWER <--> DROW
    HITOWER <--> DYLAN
    HITOWER --> OLYMPUS
    DYLAN <--> REDFERNE
    REDFERNE --> MID

    %% === MORIA CLUSTER ===
    MORIA["⛏️ MORIA\n(3900-4172)"]
    PLAINS["PLAINS\n(300-350)"]
    JUARGAN["JUARGAN\n(4700-4825)"]
    MIDENNIR["MIDENNIR\n(3500-3541)"]
    VALLEY["VALLEY\n(7800-7883)"]

    MID <--> MORIA
    MORIA <--> PLAINS
    MORIA <--> JUARGAN
    MORIA <--> MIDENNIR
    MIDENNIR --> MID
    PLAINS <--> VALLEY
    PLAINS <--> OLYMPUS

    %% === THALOS CLUSTER (via MIDENNIR) ===
    THALOS["THALOS\n(5200-5280)"]
    CANYON["CANYON\n(9201-9260)"]
    MAHNTOR["MAHNTOR\n(2300-2370)"]
    DYSTOPIA["🌆 DYSTOPIA\n(30400-30650)"]

    MIDENNIR <--> THALOS
    MIDENNIR <--> DYSTOPIA
    THALOS <--> CANYON
    THALOS <--> JUARGAN
    THALOS <--> MAHNTOR

    %% === ASTRAL CHAIN ===
    ASTRAL["ASTRAL\n(7700-7779)"]
    AIR["AIR\n(1000-1040)"]

    MAHNTOR --> ASTRAL
    ASTRAL <--> AIR
    AIR --> MID

    %% === STYLING ===
    style MID fill:#8B0000,color:#fff,stroke:#ff0000
    style HAON fill:#4B0082,color:#fff
    style MORIA fill:#2F4F4F,color:#fff
    style HITOWER fill:#4B0082,color:#fff
    style MIDENNIR fill:#2F4F4F,color:#fff
    style HELL fill:#1a0000,color:#ff6666
    style HEAVEN fill:#fffacd,color:#333
    style DYSTOPIA fill:#003366,color:#fff
    style THALOS fill:#2F4F4F,color:#fff
```

## Zone Clusters

| Cluster | Hub | Members |
|---------|-----|---------|
| Haon | HAON | Shire, Hell, Trollden, Marsh, Arachnos, Hitower |
| Hitower branches | HITOWER | Drow, Dylan ↔ Redferne, Olympus |
| Moria | MORIA | Plains ↔ Valley, Juargan, Midennir |
| Thalos | THALOS (via Midennir) | Canyon, Juargan (shared), Mahntor |
| Astral chain | — | Mahntor → Astral → Air → Midgaard (one-way loop) |
| Direct to Midgaard | — | Smurf, Heaven, School, Sewer, Dream, Mob Factory |

## Notes

- **Juargan** connects to both Moria and Thalos — it's a crossroads zone.
- **Canyon**, **Hell**, and **Heaven** are dead-ends (single connection each).
- **Dystopia** (the main city zone) connects only via Midennir, not directly to Midgaard.
- The Astral chain (Mahntor → Astral → Air → Midgaard) is a long one-way loop back to the hub.
- Midgaard's VNUM range (3000–29503) is large; Moria, Midennir, and Thalos have VNUMs that fall within it but are separate area files.
- Many areas (arena, limbo, quest, and others) are isolated with no cross-zone exits.
