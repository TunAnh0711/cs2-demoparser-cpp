import csv, os, sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)

WEAPON = {}
for line in open(os.path.join(HERE, "weapon_map.csv"), encoding="utf-8"):
    idx, name = line.rstrip("\n").split(",", 1)
    WEAPON[int(idx)] = name

def load(path):
    with open(path, newline="", encoding="utf-8") as f:
        return list(csv.DictReader(f))

def num(s):
    if s is None: return None
    if isinstance(s, bool): return float(s)
    if isinstance(s, str):
        s = s.strip()
        if s == "": return None
        if s.lower() in ("true", "false"):
            return 1.0 if s.lower() == "true" else 0.0
    try: return float(s)
    except (TypeError, ValueError): return None

demo = sys.argv[1] if len(sys.argv) > 1 else "test_demo.dem"
TICKS = set(range(0, 200)) if demo == '14155.dem' else {10000, 20000, 30000, 40000}
ours_all = load(os.path.join(ROOT, "out", "stats_testdemo.csv" if demo == "test_demo.dem"
                             else "stats_14155.csv"))
orc = load(os.path.join(HERE, "oracle_testdemo_ticks.csv" if demo == "test_demo.dem"
                         else "oracle_14155.csv"))

ours = [r for r in ours_all if int(r["tick"]) in TICKS]
o = {(int(r["tick"]), r["name"]): r for r in ours}
q = {(int(r["tick"]), r["name"]): r for r in orc}
keys = sorted(set(o) & set(q))
print(f"== {demo} ==")
print(f"ours rows(in ticks)={len(ours)}  oracle rows={len(orc)}  joined={len(keys)}  "
      f"ours-only={len(set(o)-set(q))}  oracle-only={len(set(q)-set(o))}")
if set(o)-set(q): print("  ours-only:", sorted(set(o)-set(q))[:8])
if set(q)-set(o): print("  oracle-only:", sorted(set(q)-set(o))[:8])

PAIRS = [
    ("clip", "active_weapon_ammo"), ("armor", "armor_value"), ("helmet", "has_helmet"),
    ("money", "balance"), ("ping", "ping"), ("health", "health"), ("team", "team_num"),
    ("kills", "kills_total"), ("deaths", "deaths_total"), ("assists", "assists_total"),
    ("scoped", "is_scoped"), ("defusing", "is_defusing"),
]
print(f"\n{'field':<12} {'equal':>6} {'unequal':>8} {'n/a':>6}   notes")
for our_col, orc_col in PAIRS:
    eq = bad = na = 0
    notes = []
    for k in keys:
        a = num(o[k].get(our_col)); b = num(q[k].get(orc_col))
        if b is None:
            na += 1
            if a not in (None, 0.0): notes.append((k, o[k].get(our_col), b))
            continue
        if a is None: a = 0.0
        if abs(a-b) < 1e-6: eq += 1
        else:
            bad += 1
            if len(notes) < 3: notes.append((k, o[k].get(our_col), b))
    tag = "OK  " if bad == 0 else "DIFF"
    print(f"{tag} {our_col:<10} {eq:>6} {bad:>8} {na:>6}   {notes}")

# --- weapon name ---
wmatch = wmism = 0; wsamples = []
for k in keys:
    idx = num(q[k].get("item_def_idx"))
    if idx is None: continue
    expect = WEAPON.get(int(idx), f"?{int(idx)}")
    got = o[k].get("weapon", "")
    if got == expect: wmatch += 1
    else:
        wmism += 1
        if len(wsamples) < 6: wsamples.append((k, got, expect, int(idx)))
print(f"\n{'OK  ' if wmism==0 else 'DIFF'} weapon-name  equal={wmatch} unequal={wmism}  {wsamples}")

# --- reserve ammo semantics ---
print("\n-- reserve ammo --")
agree = 0; zero_rows = []
for k in keys:
    ov = num(o[k].get("reserve")); oc = num(q[k].get("total_ammo_left"))
    ov = 0.0 if ov is None else ov
    oc = None if oc is None else oc
    if oc is None: continue
    if abs(ov-oc) < 1e-6: agree += 1
    elif oc == 0.0: zero_rows.append((k, int(num(q[k]["item_def_idx"])), WEAPON.get(int(num(q[k]["item_def_idx"])),"?"), int(ov), int(num(o[k]["clip"]))))
print(f"rows where reference reserve == ours: {agree}/{len(keys)}")
print(f"rows where reference reserve == 0 but ours differs: {len(zero_rows)}")
print(f"  {'tick':>6} {'name':<20} {'weapon':<14} {'oursReserve':>11} {'clip':>5} {'oursTotal':>9}  cs2Max")
MAXRES = {"AK-47":120,"M4A4":120,"Galil AR":120,"FAMAS":120,"SG 553":120,"SSG 08":100,"AWP":40,
          "Glock-18":140,"P2000":73,"USP-S":36,"P250":39,"Desert Eagle":42,"MAC-10":130,
          "MP5-SD":120,"UMP-45":120,"MP9":120,"M4A1-S":100,"XM1014":64,"MAG-7":140,"Nova":64,
          "Sawed-Off":128,"CZ75-Auto":24,"R8 Revolver":16,"P90":100,"MP7":60,"PP-Bizon":90,
          "Bowie Knife":0,"M9 Bayonet":0,"Huntsman Knife":0}
for t, idx, wpn, res, clip in zero_rows:
    mx = MAXRES.get(wpn)
    tot = res + clip
    ok = "OK" if (mx is None or tot <= mx) else "OVER"
    print(f"  {t[0]:>6} {t[1]:<20} {wpn:<14} {res:>11} {clip:>5} {tot:>9}  {mx if mx is not None else "-"}  {ok}")
