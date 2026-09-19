//
// Stands in for net_gui.c and net_dedicated.c, neither of which was imported.
// The first needs the textscreen library, which is not in this build; the
// second is a mode a console will never run.
//
// All net_gui.c really does is pump the client and server while the game has
// not launched yet, and start it once enough players have joined. That part is
// here. The lobby it drew is replaced by dg_net_wait, which paints on the
// loader frame -- D_ConnectNetGame runs before I_InitGraphics, so DOOM has no
// screen of its own yet.
//
// Its WAD and dehacked checksum warning went with the lobby, and nothing else
// catches a mismatch -- the server forwards the sums but never refuses anyone.
// A player who joins on the wrong WAD is not told; the game just desyncs.
//

#include <stdlib.h>

#include "doomtype.h"
#include "i_system.h"
#include "i_timer.h"
#include "m_argv.h"
#include "m_misc.h"
#include "net_client.h"
#include "net_dedicated.h"
#include "net_gui.h"
#include "net_server.h"

#include "dg_netui.h"

// From -nodes: launch as soon as this many have joined. 0 means the player
// starts it by hand, which on this console means pressing Cross.
static int expected_nodes;

// The server only honours a launch from the controller -- the first player to
// join -- so everyone else has the prompt hidden and the button ignored.
static boolean IsController(void)
{
    return net_client_received_wait_data && net_client_wait_data.is_controller;
}

static void CheckAutoLaunch(void)
{
    int nodes;

    if (IsController() && expected_nodes > 0)
    {
        nodes = net_client_wait_data.num_players
              + net_client_wait_data.num_drones;

        if (nodes >= expected_nodes)
        {
            NET_CL_LaunchGame();
            expected_nodes = 0;
        }
    }
}

void NET_WaitForLaunch(void)
{
    char line1[48], line2[48];
    int i;

    //!
    // @arg <n>
    // @category net
    //
    // Autostart the netgame when n nodes (clients) have joined the server.
    //

    i = M_CheckParmWithArgs("-nodes", 1);
    if (i > 0)
    {
        expected_nodes = atoi(myargv[i + 1]);
    }

    dg_net_wait_reset();

    while (net_waiting_for_launch)
    {
        CheckAutoLaunch();

        NET_CL_Run();
        NET_SV_Run();

        // Once a second, so a lobby that dies at the 30s connection timeout
        // shows whether anything was arriving at all.
        {
            static int last_beat;
            int now = I_GetTimeMS();

            if (now - last_beat > 1000)
            {
                last_beat = now;
                printf("NET: lobby waitdata=%d players=%d drones=%d "
                       "ready=%d controller=%d connected=%d\n",
                       net_client_received_wait_data,
                       net_client_wait_data.num_players,
                       net_client_wait_data.num_drones,
                       net_client_wait_data.ready_players,
                       net_client_wait_data.is_controller,
                       net_client_connected);
            }
        }

        if (!net_client_connected)
        {
            I_Error("Lost connection to server");
        }

        if (net_client_received_wait_data)
        {
            M_snprintf(line1, sizeof(line1), "%d PLAYER%s CONNECTED",
                       net_client_wait_data.num_players,
                       net_client_wait_data.num_players == 1 ? "" : "S");
            M_StringCopy(line2,
                         IsController() ? "X  START NOW      O  CANCEL"
                                        : "WAITING FOR THE HOST TO START",
                         sizeof(line2));
        }
        else
        {
            M_StringCopy(line1, "JOINING", sizeof(line1));
            M_StringCopy(line2, "O  CANCEL", sizeof(line2));
        }

        switch (dg_net_wait("NETGAME LOBBY", line1, line2))
        {
          case DG_NET_CANCEL:
            I_Error("Netgame cancelled.");
            break;

          case DG_NET_START:
            if (IsController())
            {
                NET_CL_LaunchGame();
            }
            break;
        }
    }
}

void NET_DedicatedServer(void)
{
    I_Error("Dedicated server mode is not supported on PS5.");
}
