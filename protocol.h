
#define MAX_PLAYERS 4

void InitProtocol(doomcom_t far *dc);

// Invoked when Sopwith wants to run another tic. The caller passes the
// command for the next tic being made, and if successful, cmds[] is
// populated with the commands for all players. If not successful (because
// the commands for the next tic have not yet been received from all other
// players), false is returned, and the caller will need to try again later.
// Even if unsuccessful, the command will still be registered and sent to
// the other players in the game.
bool SwapCommand(uint16_t cmd, uint16_t cmds[MAX_PLAYERS]);

// Poll driver to receive and process new packets.
void ReceivePackets(void);
