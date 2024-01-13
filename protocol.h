
#define MAX_PLAYERS 4

void InitProtocol(doomcom_t far *dc);
bool SwapCommand(uint16_t cmd, uint16_t cmds[MAX_PLAYERS]);

// Poll driver to receive and process new packets.
void ReceivePackets(void);
