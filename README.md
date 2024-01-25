
![Imaginot logo](imaginot.svg)

This is an adapter program for the DOS versions of
[Sopwith](https://en.wikipedia.org/wiki/Sopwith\_%28video_game%29)
to make its multiplayer feature work. In particular, it can be played
over the
[IPX protocol](https://en.wikipedia.org/wiki/Internetwork_Packet_Exchange)
like most other DOS games. This allows multiplayer
Sopwith to be played over the Internet in [DOSbox](https://www.dosbox.com/).

A demo video [can be found here](https://www.youtube.com/watch?v=qgHfTwQQ8u8).

## Background

Almost everyone who has played Sopwith has noticed the "Multiplayer"
option on the main menu. Anyone curious enough to have tried to get it
to work has invariably failed. It seemed like a broken feature.

In fact, it is not broken; you just need some very unusual networking
hardware to use it. Sopwith was written as a demo program for BMB
Compuscience's
["Imaginet" networking system](https://fragglet.github.io/sdl-sopwith/history.html#imaginet).
Imaginet allowed multiple computers to share a single disk drive to
exchange data; Sopwith's multiplayer feature uses a shared file that is
read and written by multiple computers to communicate. However, since it
uses raw sector-level disk access, it is not possible to make it work
using something like Windows file sharing.

NY00123 [recently had some success](https://www.youtube.com/watch?v=HxEBEqbuIqI)
getting Sopwith's multiplayer mode to work, by having multiple QEMU
virtual machines share a single floppy disk image. The game runs slow
and clunky, and can only work on a single machine; however, it gave
some hope to the idea that it might be possible to get the feature
working properly at long last. In particular it was encouraging
because it showed that only the standard PC hardware interfaces
are needed; there is no need for any special drivers or emulation of
the BMB hardware.

## Imaginot

Imaginot builds on NY00123's discovery by providing a full adapter
program that allows Sopwith to be played using a normal networking
stack. To accomplish this, it has to do some low-level hackery to
"trick" Sopwith into thinking it's using the BMB hardware:

* The program hooks a number of interrupt handlers: the PC BIOS API
  (interrupt 13h), the DOS API (interrupt 21h) and the DOS low-level
  sector read/write APIs (interrupts 25h and 26h). The goal here is to
  emulate a floppy disk drive B: and respond to the API calls that
  Sopwith uses to read and write to it.

* When Sopwith reads from the virtual disk, it sees what looks like a
  FAT12 file system containing the files it expects. Reads and writes
  to these "files" are interpreted by the driver, which has special
  knowledge of their format (thanks to the Sopwith source code having
  been open sourced many years back!)

* Sopwith's networking is supposed to work by different machines
  reading and writing to a shared file.
  This is inherently a [stop and wait](https://en.wikipedia.org/wiki/Stop-and-wait_ARQ)
  protocol, which does not work well over long-distance networks.
  Instead, *Imaginot* works by sending a pipeline of the input commands
  between the different peers in the game. This approach helps to make
  the game more playable over the Internet.

* *Imaginot* reuses the [driver interface from Doom](https://doomwiki.org/wiki/Doom_networking_component).
  This allows Sopwith to be played over a number of different networking
  protocols and technologies: Serial/Modem, IPX, Parallel Port, Serial
  Infrared and UDP, to name a few found in the
  [Vanilla Utilities](https://github.com/fragglet/vanilla-utilities)
  collection. Included are drivers for IPX and UDP.

