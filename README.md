# LightweightMC 🚀

**LightweightMC** est un moteur de serveur Minecraft alternatif, développé de zéro en **C++20**. Conçu spécifiquement pour s'exécuter sur du matériel à ressources très limitées (SBC ARM, Raspberry Pi, TV Box Android flashées), il abandonne l'architecture JVM classique au profit d'une approche **100 % événementielle** et de la **résolution temporelle à la demande**.

---

## 🔑 Caractéristiques Principales

* **Zéro JVM, Zéro Garbage Collector :** Binaire natif C++20 avec empreinte RAM minimale (< 30 Mo au démarrage).
* **Architecture 100 % Événementielle :** Gestion du réseau basée sur `epoll` / non-bloquant pour encaisser les connexions sans bloquer le thread principal.
* **Timestamp Catch-up (Lazy Evaluation) :** Suppression des boucles de *ticks* sur les chunks déchargés. Le calcul des fours, cultures et inventaires s'effectue par différence temporelle ($\Delta t$) au réveil.
* **Format I/O sur mesure (`.mcc`) :** Stockage hybride des chunks combinant **SQLite** et compression ultra-rapide **LZ4**.
* **Gestion Stricte du Rayon de Chunks :**
  * `Active` (0 à 12 chunks) : Simulation complète et ticks temps réel sur toute la distance d'affichage standard.
  * `Frozen` (> 12 chunks en bordure de cache) : Présents en RAM pour l'affichage, IA & Redstone en pause.
  * `Unloaded` (Hors de portée) : Sauvegardés sur disque avec horodatage UNIX.

---

## 📊 Matériel Cible

Optimisé pour les micro-serveurs et SBC (Single Board Computers) :
* **SoC :** ARMv7 / ARMv8 (Raspberry Pi 2/3/4, RK3318, Allwinner, Orange Pi).
* **RAM requise :** < 256 Mo libres.
* **OS :** Linux (Debian, Armbian, Alpine).

---

## 🛠️ Compilation

### Prérequis
* GCC 11+ ou Clang 13+ (support C++20)
* CMake 3.20+
* `liblz4-dev` & `libsqlite3-dev`

```bash
# Cloner le dépôt
git clone [https://github.com/ton-compte/LightweightMC.git](https://github.com/ton-compte/LightweightMC.git)
cd LightweightMC

# Préparer le build
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release ..

# Compiler
make -j$(nproc)
