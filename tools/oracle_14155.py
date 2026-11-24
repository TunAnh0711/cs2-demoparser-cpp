"""Regenerate the demoparser2 oracle dataset for the 14155 demo.

14155.dem is a pistol-round bot demo: nine of the ten players are bots with
m_steamID == 0, so the reference parser (which keys its player list off the
userinfo string table) only exposes the human player, "Partridge".  Our parser
reports all ten controller entities; the validation joins on (tick, name), so
the nine extra rows simply have no oracle counterpart.

Usage:  python tools/oracle_14155.py
"""

import os

from demoparser2 import DemoParser

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)

PROPS = [
    "active_weapon_ammo",
    "item_def_idx",
    "total_ammo_left",
    "armor_value",
    "health",
    "has_helmet",
    "is_scoped",
    "is_defusing",
    "balance",
    "ping",
    "team_num",
    "kills_total",
    "deaths_total",
    "assists_total",
]
TICKS = list(range(0, 200))


def main() -> None:
    parser = DemoParser(os.path.join(ROOT, "testdata", "14155.dem"))
    frame = parser.parse_ticks(PROPS, ticks=TICKS)
    out = os.path.join(HERE, "oracle_14155.csv")
    frame.to_csv(out, index=False)
    print(f"shape={frame.shape} -> {out}")


if __name__ == "__main__":
    main()
