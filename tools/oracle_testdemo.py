"""Regenerate the demoparser2 oracle dataset for the reference test demo.

The reference implementation (LaihoE/demoparser, exposed through the
demoparser2 wheel) is the golden oracle for this parser.  It is queried with
the same props the ESP extracts and with the exact tick set the upstream
e2e test uses (0, 10000, 20000, 30000, 40000) so that the two data sets can be
joined on (tick, name).

Usage:  python tools/oracle_testdemo.py
"""

import os

from demoparser2 import DemoParser

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)

PROPS = [
    "active_weapon_ammo",      # m_iClip1                 (weapon)
    "item_def_idx",            # m_iItemDefinitionIndex   (weapon)
    "total_ammo_left",         # m_pReserveAmmo           (weapon)
    "armor_value",             # CCSPlayerPawn.m_ArmorValue
    "health",                  # CCSPlayerPawn.m_iHealth
    "has_helmet",              # CCSPlayerPawn.CCSPlayer_ItemServices.m_bHasHelmet
    "is_scoped",               # CCSPlayerPawn.m_bIsScoped
    "is_defusing",             # CCSPlayerPawn.m_bIsDefusing
    "balance",                 # CCSPlayerController...InGameMoneyServices.m_iAccount
    "ping",                    # CCSPlayerController.m_iPing
    "team_num",                # CCSPlayerPawn.m_iTeamNum
    "kills_total",             # ...ActionTrackingServices.m_iKills
    "deaths_total",
    "assists_total",
]
TICKS = [0, 10000, 20000, 30000, 40000]


def main() -> None:
    parser = DemoParser(os.path.join(ROOT, "testdata", "test_demo.dem"))
    frame = parser.parse_ticks(PROPS, ticks=TICKS)
    out = os.path.join(HERE, "oracle_testdemo_ticks.csv")
    frame.to_csv(out, index=False)
    print(f"shape={frame.shape} -> {out}")


if __name__ == "__main__":
    main()
