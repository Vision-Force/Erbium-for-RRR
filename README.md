# Erbium for RRR (Rocket Racing Reborn) - Rocket Racing

**This is a modified version of Erbium forked from [Erbium](https://github.com/plooshi/Erbium). Erbium itself is by plooshi https://github.com/plooshi**
**Modified by Shrezee https://github.com/shrezesUverse in 2026** for Rocket Racing Reborn project, which is a preservation project for
Fortnite 30.40, its Rocket Racing (internally DelMar) mode

Erbium is GPL-3.0. So is this as well. The licence text is
unchanged in `LICENSE`, and this notice records the modification as GPL-3.0 section 5(a) requires. basically the only reason this repo exists is so that Vision Force can use this Erbium-RRR.dll

<img src="https://raw.githubusercontent.com/Vision-Force/.github/main/profile/assets/divider-wide.svg" width="100%" alt="Vision Force Studio" />

### What was changed

| File | Lines | What |
|---|---|---|
| [`FortniteGame/Private/DelMar.cpp`](https://github.com/Vision-Force/Erbium-for-RRR/blob/main/Erbium/FortniteGame/Private/DelMar.cpp) | [**+4936**](https://github.com/Vision-Force/Erbium-for-RRR/compare/afed563...main#diff-5018c87a71a5501551ea8dd7cd072240aead5f60e6af77bc18b53055d8d37cd6) | new - the DelMar bring-up |
| [`FortniteGame/Public/DelMar.h`](https://github.com/Vision-Force/Erbium-for-RRR/blob/main/Erbium/FortniteGame/Public/DelMar.h) | [**+63**](https://github.com/Vision-Force/Erbium-for-RRR/compare/afed563...main#diff-48892d86e422f9c0c347b3846ae6c898564fa3cb11158311cda7f9dbc9a3524b) | new - its header |
| [`Erbium/Public/Configuration.h`](https://github.com/Vision-Force/Erbium-for-RRR/blob/main/Erbium/Erbium/Public/Configuration.h) | [**+99** &minus;7](https://github.com/Vision-Force/Erbium-for-RRR/compare/afed563...main#diff-919d3546d8dd14cf802367497b1f38fcfe89f6d7c917a330edca3d7799302774) | `bDelMar*` flag family and 30.40 pins |
| [`Erbium/Private/dllmain.cpp`](https://github.com/Vision-Force/Erbium-for-RRR/blob/main/Erbium/Erbium/Private/dllmain.cpp) | [**+116** &minus;30](https://github.com/Vision-Force/Erbium-for-RRR/compare/afed563...main#diff-2930119ee154f4e73ea8296a66d79b1827f9c97c5f07fc79017cefaaa716ebc3) | 30.40 entry points |
| [`Erbium/Private/Finders.cpp`](https://github.com/Vision-Force/Erbium-for-RRR/blob/main/Erbium/Erbium/Private/Finders.cpp) | [**+6**](https://github.com/Vision-Force/Erbium-for-RRR/compare/afed563...main#diff-3e02fbe77788047a9fe27158574515fc0bae532e476e671fd12a1dba1aabfc6c) | finder pins |
| [`Erbium/Private/Misc.cpp`](https://github.com/Vision-Force/Erbium-for-RRR/blob/main/Erbium/Erbium/Private/Misc.cpp) | [**+12** &minus;3](https://github.com/Vision-Force/Erbium-for-RRR/compare/afed563...main#diff-6af38c4905c8e773a7f15ae72ee5c6dc0ae3ddedc5a38c61ab4692cd792b22c2) | finder pins |
| [`FortniteGame/Private/FortGameMode.cpp`](https://github.com/Vision-Force/Erbium-for-RRR/blob/main/Erbium/FortniteGame/Private/FortGameMode.cpp) | [**+41** &minus;58](https://github.com/Vision-Force/Erbium-for-RRR/compare/afed563...main#diff-cebc71d6cb7aa2471eae1491ca70fcc0ee0799768ba31f70075d6f97683719db) | non BR game state anyway |
| [`FortniteGame/Private/FortPlayerControllerAthena.cpp`](https://github.com/Vision-Force/Erbium-for-RRR/blob/main/Erbium/FortniteGame/Private/FortPlayerControllerAthena.cpp) | [**+26** &minus;12](https://github.com/Vision-Force/Erbium-for-RRR/compare/afed563...main#diff-6b66404220162e2f39431a5bfbe4bc3837255dedde9fecf283e9b250ad04bbe0) | non BR game state anyway |
| [`FortniteGame/Private/FortPlayerPawnAthena.cpp`](https://github.com/Vision-Force/Erbium-for-RRR/blob/main/Erbium/FortniteGame/Private/FortPlayerPawnAthena.cpp) | [**+8** &minus;3](https://github.com/Vision-Force/Erbium-for-RRR/compare/afed563...main#diff-e1f5476602cfd55574826dd3b70963de9d470b014b6003f84b3cbd6c4b66fa1f) | non BR game state anyway |
| [`FortniteGame/Private/FortPhysicsPawn.cpp`](https://github.com/Vision-Force/Erbium-for-RRR/blob/main/Erbium/FortniteGame/Private/FortPhysicsPawn.cpp) | [**+9**](https://github.com/Vision-Force/Erbium-for-RRR/compare/afed563...main#diff-f1434ec7a1ddcf3bcfe71c96e3958cc37b01e65be83c5a294d8fccb6aef99066) | non BR game state anyway |
| [`FortniteGame/Private/FortQuestManager.cpp`](https://github.com/Vision-Force/Erbium-for-RRR/blob/main/Erbium/FortniteGame/Private/FortQuestManager.cpp) | [**+5** &minus;2](https://github.com/Vision-Force/Erbium-for-RRR/compare/afed563...main#diff-68e9c9ec990a1a4014157fcfddca3c980dd48f1170126b27c15470ebcec860a2) | non BR game state anyway |
| [`FortniteGame/Private/FortKismetLibrary.cpp`](https://github.com/Vision-Force/Erbium-for-RRR/blob/main/Erbium/FortniteGame/Private/FortKismetLibrary.cpp) | [**+3** &minus;1](https://github.com/Vision-Force/Erbium-for-RRR/compare/afed563...main#diff-7cbc2b0712568b22ad73115d061e69deae95c83a2536980f005889fb40db566c) | non BR game state anyway |
| [`FortniteGame/Private/FortVehicleSeatWeaponComponent.cpp`](https://github.com/Vision-Force/Erbium-for-RRR/blob/main/Erbium/FortniteGame/Private/FortVehicleSeatWeaponComponent.cpp) | [**+4** &minus;1](https://github.com/Vision-Force/Erbium-for-RRR/compare/afed563...main#diff-c9d33faedf0ecbfda7819b7844f348ed636686b9deb6f419a17c077946be3c6b) | non BR game state anyway |
| [`FortniteGame/Private/BattleRoyaleGamePhaseLogic.cpp`](https://github.com/Vision-Force/Erbium-for-RRR/blob/main/Erbium/FortniteGame/Private/BattleRoyaleGamePhaseLogic.cpp) | [**+6**](https://github.com/Vision-Force/Erbium-for-RRR/compare/afed563...main#diff-ad3cb0e52daa189ddf1e4aa297e1efb69d5df66d88b5291b9572bdfe21129635) | non BR game state anyway |
| [`Engine/Private/NetDriver.cpp`](https://github.com/Vision-Force/Erbium-for-RRR/blob/main/Erbium/Engine/Private/NetDriver.cpp) | [**+2** &minus;1](https://github.com/Vision-Force/Erbium-for-RRR/compare/afed563...main#diff-05753c44bc050f67536775dd3d44631b0116410b45db4c4121fcecab4d3c563c) | 30.40 net driver pin |
| [`Erbium/Plugins/CrashReporter/Private/CrashReporter.cpp`](https://github.com/Vision-Force/Erbium-for-RRR/blob/main/Erbium/Erbium/Plugins/CrashReporter/Private/CrashReporter.cpp) | [**+4**](https://github.com/Vision-Force/Erbium-for-RRR/compare/afed563...main#diff-9cb7b306bc4d0462bfbdd8606c53dafe3968c5890e81c8d2630714c854caec8f) | let the VEH cooperate with our SEH guards |
| [`Erbium.vcxproj`](https://github.com/Vision-Force/Erbium-for-RRR/blob/main/Erbium/Erbium.vcxproj) | [**+4** &minus;1](https://github.com/Vision-Force/Erbium-for-RRR/compare/afed563...main#diff-1d21c7191340fd39be6dd32dbe1d3f51a0ccebaf40a08557e72a33cbe41543a3) | vc |
| | **+5344** &minus;119 | across 17 files |

[compare against upstream](https://github.com/Vision-Force/Erbium-for-RRR/compare/afed563...main)

Built for Fortnite 30.40 (CL 35235494) only,

<img src="https://raw.githubusercontent.com/Vision-Force/.github/main/profile/assets/divider-wide.svg" width="100%" alt="Vision Force Studio" />

### Erbium

Erbium is a WIP universal gameserver for Fortnite.

[**Join their Discord!**](https://discord.gg/WxNEGBxfKq)

<img src="https://raw.githubusercontent.com/Vision-Force/.github/main/profile/assets/divider-wide.svg" width="100%" alt="Vision Force Studio" />

### Credits
Credit to plooshi (Sarah) for Erbium, and to everyone whose commits are in this history: Special thanks to Mariki - My goat.
and the others:
BeRightBack, ralz, Itztiva and Andrew. Credit to Milxnor for parts of `Finders.cpp`, as the original
README asks

<a href="https://github.com/plooshi"><img src="https://raw.githubusercontent.com/Vision-Force/Erbium-for-RRR/main/assets/contributors/plooshi.svg" width="420" alt="plooshi" /></a>

Erbium is plooshi's. 905 commits under this project are hers - all but a handful - and everything Rocket Racing Reborn
does is made on top of that. The rest of the people whose commits are in this history:

<p>
<a href="https://github.com/ShrezesUverse"><img src="https://raw.githubusercontent.com/Vision-Force/Erbium-for-RRR/main/assets/contributors/shrezesuverse.svg" width="200" alt="ShrezesUverse" /></a>
<a href="https://github.com/andr1ww"><img src="https://raw.githubusercontent.com/Vision-Force/Erbium-for-RRR/main/assets/contributors/andr1ww.svg" width="200" alt="andr1ww" /></a>
<a href="https://github.com/BeRightBack0"><img src="https://raw.githubusercontent.com/Vision-Force/Erbium-for-RRR/main/assets/contributors/berightback0.svg" width="200" alt="BeRightBack0" /></a>
<a href="https://github.com/Ralzify"><img src="https://raw.githubusercontent.com/Vision-Force/Erbium-for-RRR/main/assets/contributors/ralzify.svg" width="200" alt="Ralzify" /></a>
<a href="https://github.com/Mar1ki"><img src="https://raw.githubusercontent.com/Vision-Force/Erbium-for-RRR/main/assets/contributors/mar1ki.svg" width="200" alt="Mar1ki" /></a>
<a href="https://github.com/Milxnor"><img src="https://raw.githubusercontent.com/Vision-Force/Erbium-for-RRR/main/assets/contributors/milxnor.svg" width="200" alt="Milxnor" /></a>
<a href="https://github.com/egator6"><img src="https://raw.githubusercontent.com/Vision-Force/Erbium-for-RRR/main/assets/contributors/egator6.svg" width="200" alt="egator6" /></a>
<a href="https://github.com/gavbowersdomain"><img src="https://raw.githubusercontent.com/Vision-Force/Erbium-for-RRR/main/assets/contributors/gavbowersdomain.svg" width="200" alt="gavbowersdomain" /></a>
<a href="https://github.com/blurrfx"><img src="https://raw.githubusercontent.com/Vision-Force/Erbium-for-RRR/main/assets/contributors/blurrfx.svg" width="200" alt="blurrfx" /></a>
<a href="https://github.com/defcharles"><img src="https://raw.githubusercontent.com/Vision-Force/Erbium-for-RRR/main/assets/contributors/defcharles.svg" width="200" alt="defcharles" /></a>
<a href="https://github.com/1lunarxx"><img src="https://raw.githubusercontent.com/Vision-Force/Erbium-for-RRR/main/assets/contributors/1lunarxx.svg" width="200" alt="1lunarxx" /></a>
<a href="https://github.com/max8447"><img src="https://raw.githubusercontent.com/Vision-Force/Erbium-for-RRR/main/assets/contributors/max8447.svg" width="200" alt="max8447" /></a>
</p>

<img src="https://raw.githubusercontent.com/Vision-Force/.github/main/profile/assets/divider-wide.svg" width="100%" alt="Vision Force Studio" />

Support me
<a href="https://ko-fi.com/shrezee">
  <img src="https://img.shields.io/badge/Ko--fi-Shrezee-47d1ff?style=for-the-badge&logo=kofi&logoColor=white&labelColor=0d1a2e" alt="Support Me"/>
</a>

<br/><br/>

<sub>Appreciate that</sub>

<br/>
