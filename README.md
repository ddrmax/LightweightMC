# LightweightMC
LightweightMC est un moteur de serveur Minecraft alternatif développé de zéro en C++20 pour SBC (Raspberry Pi, TV Box ARM). 100% événementiel et nativement zéro JVM, il remplace la boucle de tick par du calcul temporel ($\Delta t$), un format binaire custom compressé en LZ4 et une persistance sous SQLite. Empreinte RAM &lt; 30 Mo.
